#include "ConnectionManager.h"
#include <iostream>
#include <stdexcept>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>



ConnectionManager::~ConnectionManager() {
    stop_listening();
    
    std::lock_guard<std::mutex> lock(connectionMapMutex_);
    for (const auto& [key, sock] : connectionMap_) {
        close(sock);
    }
    connectionMap_.clear();
}

void ConnectionManager::stop_listening() {
    {
        std::lock_guard<std::mutex> lock(listeningMutex_);
        isListening_ = false;
        if (listeningSocket_ >= 0) {
            close(listeningSocket_);
            listeningSocket_ = -1;
        }
    }
}



void ConnectionManager::start_listening() {
    listeningSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listeningSocket_ < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(localPort_);
    serverAddress.sin_addr.s_addr = inet_addr(localAddress_.c_str());

    if (bind(listeningSocket_, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        close(listeningSocket_);
        throw std::runtime_error("Failed to bind");
    }

    if (listen(listeningSocket_, 10) < 0) {  // Listen with a backlog of 10 connections
        close(listeningSocket_);
        throw std::runtime_error("Failed to listen");
    }

    isListening_ = true;

    while (isListening_) {
        std::cout << "Listening Attentively"<< "\n";
        int clientSocket = accept(listeningSocket_, nullptr, nullptr);
        if (clientSocket < 0) {
            std::cerr << "Failed to accept connection" << std::endl;
            continue;
        }

        // Lambda task for processing the request, capturing clientSocket by value
        auto task = [this, clientSocket]() {
            while(true){
                try {
                    process_request(clientSocket);
                } catch (const std::exception& e) {
                    std::cerr << "Error processing request: " << e.what() << std::endl;
                }
            }
        };

        
        threadPool_.enqueue(task);
    }

    close(listeningSocket_);  
}



int ConnectionManager::connect_to(const std::string& destAddress, int destPort) {
    
    // check if ther eis already a connextion
    auto key = std::make_pair(destAddress, destPort);

    {
        std::lock_guard<std::mutex> lock(connectionMapMutex_);
        
        auto it = connectionMap_.find(key);
        if (it != connectionMap_.end()) {
            // Verify connection is still valid
            int error = 0;
            socklen_t len = sizeof(error);
            int retval = getsockopt(it->second, SOL_SOCKET, SO_ERROR, &error, &len);
            
            if (retval == 0 && error == 0) {
                std::cout << "Existing connection found - ";
                return it->second;
            }
            
            // Connection is dead, remove it
            close(it->second);
            connectionMap_.erase(it);
        }
    }

    // Create new connection 
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(destPort);
    if (inet_pton(AF_INET, destAddress.c_str(), &serverAddress.sin_addr) <= 0) {
        close(sock);
        throw std::runtime_error("Invalid address format");
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0) {
        close(sock);
        throw std::runtime_error("Failed to connect to server at " + destAddress + ":" + std::to_string(destPort));
    }

    // store the new connection
    {
        std::lock_guard<std::mutex> lock(connectionMapMutex_);
        connectionMap_[key] = sock;
    }

    return sock;
}

void ConnectionManager::send_all(int socket, const std::string& data) {
    size_t totalSent = 0;
    while (totalSent < data.size()) {
       
        ssize_t sent = send(socket, data.data() + totalSent, data.size() - totalSent, 0);
        if (sent < 0) {
            if (errno == EPIPE) {
                std::cerr << "SIGPIPE: Peer closed the connection." << std::endl;
                throw std::runtime_error("Socket closed by peer");
            }
            throw std::runtime_error("Failed to send data to socket");
        }
                totalSent += sent;
        std::cout << "Sent "<< totalSent << "/" << data.size() <<"\n";
    }
}

void ConnectionManager::receive_all(int socket, char* buffer, size_t size) {
    size_t totalReceived = 0;
    while (totalReceived < size) {
        ssize_t received = recv(socket, buffer + totalReceived, size - totalReceived, 0);
        if (received < 0) {
            throw std::runtime_error("Failed to receive data from socket");
        } else if (received == 0) {
            throw std::runtime_error("Connection closed unexpectedly");
        }
        totalReceived += received;
        std::cout << "Recieved "<< totalReceived << "/" << size <<"\n";
    }
}


// Update process_request to handle piece requests
void ConnectionManager::process_request(int clientSocket) {
    RequestHeader header;
    receive_all(clientSocket, reinterpret_cast<char*>(&header), sizeof(RequestHeader));

    switch (header.type) {
        case META_REQ:
            process_meta_request(clientSocket, header);
            break;
        case PIECE_REQ:
            process_piece_request(clientSocket, header);
            break;
        default:
            throw std::runtime_error("Unknown request type");
    }
}

FileMetaData ConnectionManager::request_metadata(const std::string& destAddress, int destPort) {
    int sock = connect_to(destAddress, destPort);
    std::cout << "Connected to: " << destAddress<<":"<< destPort<<"\n";

    RequestHeader header = {META_REQ, 0};
    std::vector<char> serializedHeader = header.serialize();
    send_all(sock, std::string(serializedHeader.begin(), serializedHeader.end()));

    std::cout << "Send Meta data request\n";

    // Receive the response header
    RequestHeader responseHeader;
    receive_all(sock, reinterpret_cast<char*>(&responseHeader), sizeof(RequestHeader));

    std::cout << "Recieved meta data\n";

    // Ensure the response is of type META_RES
    if (responseHeader.type != META_RES) {
        close(sock);
        throw std::runtime_error("Unexpected response type");
    }

    // Receive the metadata payload
    std::vector<char> payloadBuffer(responseHeader.payloadSize);
    receive_all(sock, payloadBuffer.data(), responseHeader.payloadSize);


    return FileMetaData::deserialize(payloadBuffer);
}

void ConnectionManager::close_connection(const std::string& destAddress, int destPort) {
    auto key = std::make_pair(destAddress, destPort);
    auto it = connectionMap_.find(key);
    if (it != connectionMap_.end()) {
        close(it->second);  
        connectionMap_.erase(it);  
    }
}



std::string ConnectionManager::request_piece(const std::string& destAddress, int destPort, size_t pieceIndex) {
    int sock = connect_to(destAddress, destPort);
    std::cout << "Connected to: " << destAddress<<":"<< destPort<<"\n";


    // Prepare piece request
    PieceRequest request{static_cast<size_t>(pieceIndex)};
    std::vector<char> serializedRequest = request.serialize();
    
    // Send request header followed by piece index
    RequestHeader header = {PIECE_REQ, static_cast<uint32_t>(serializedRequest.size())};
    std::vector<char> serializedHeader = header.serialize();
    
    send_all(sock, std::string(serializedHeader.begin(), serializedHeader.end()));
    std::cout << "Send Header \n";
    send_all(sock, std::string(serializedRequest.begin(), serializedRequest.end()));
    std::cout << "Send Piece Request \n";

    // Receive response header
    RequestHeader responseHeader;
    receive_all(sock, reinterpret_cast<char*>(&responseHeader), sizeof(RequestHeader));
    std::cout << "Reiveived Piece response \n";

    if (responseHeader.type != PIECE_RES) {
        throw std::runtime_error("Unexpected response type for piece request");
    }

    // Receive piece data
    std::vector<char> pieceBuffer(responseHeader.payloadSize);
    receive_all(sock, pieceBuffer.data(), responseHeader.payloadSize);
    std::cout << "Reiveived Piece data \n";
    
    return std::string(pieceBuffer.begin(), pieceBuffer.end());
}

void ConnectionManager::process_piece_request(int clientSocket, const RequestHeader& header) {
    if (!fileManager_) {
        throw std::runtime_error("Cannot serve piece request: no FileManager available");
    }

    std::cout << "Processing Piece Request \n";

    // Receive piece index
    std::vector<char> requestBuffer(header.payloadSize);
    receive_all(clientSocket, requestBuffer.data(), header.payloadSize);
    PieceRequest request = PieceRequest::deserialize(requestBuffer);

    std::cout << "Recieved Piece rquest of size  \n";

    std::string pieceData = fileManager_->send(request.pieceIndex);
    
    RequestHeader responseHeader = {PIECE_RES, static_cast<uint32_t>(pieceData.size())};
    std::vector<char> serializedHeader = responseHeader.serialize();
    
    send_all(clientSocket, std::string(serializedHeader.begin(), serializedHeader.end()));
    std::cout << "Sent Piece response \n";
    send_all(clientSocket, std::string(pieceData.begin(), pieceData.end()));
     std::cout << "Sent Piece data \n";
}


void ConnectionManager::process_meta_request(int clientSocket, const RequestHeader& header) {
    (void) header;  

    if (!fileManager_) {
        throw std::runtime_error("Cannot serve metadata request: no FileManager available");
    }

    std::cout << "Got meta data request\n";

    // Get serialized metadata as a string
    std::string serializedData = fileManager_->get_metadata().serialize();
    
    // Prepare the response header
    RequestHeader responseHeader = {META_RES, static_cast<uint32_t>(serializedData.size())};
    std::vector<char> serializedHeader = responseHeader.serialize();

    // Send serialized header and serialized data
    send_all(clientSocket, std::string(serializedHeader.begin(), serializedHeader.end()));
    std::cout << "SENT META header\n";
    send_all(clientSocket, serializedData); 
}
