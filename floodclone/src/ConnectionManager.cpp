#include "ConnectionManager.h"
#include <iostream>
#include <stdexcept>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>



ConnectionManager::~ConnectionManager() {
    stop_listening();
    
    std::lock_guard<std::mutex> lock(connectionMapMutex_);
    for (const auto& [key, fd] : connectionMap_) {
        close(fd);
    }
    connectionMap_.clear();
    
    std::lock_guard<std::mutex> lockMap(fdLocksMapMutex_);
    fdLocks_.clear();
}

void ConnectionManager::stop_listening() {
    isListening_ = false;
}



void ConnectionManager::start_listening() {
    
    
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        throw std::runtime_error("Failed to create epoll instance");
    }

    // Create and bind listening socket as before
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

    if (listen(listeningSocket_, SOMAXCONN) < 0) {
        close(listeningSocket_);
        throw std::runtime_error("Failed to listen");
    }

    // Add listening socket to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listeningSocket_;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listeningSocket_, &ev) == -1) {
        throw std::runtime_error("Failed to add listening socket to epoll");
    }

    isListening_ = true;
    const int MAX_EVENTS = 32;
    struct epoll_event events[MAX_EVENTS];

    while (isListening_) {

        // Wait for events
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            throw std::runtime_error("epoll_wait failed");
        }

        // Process the sockets that have events
        for (int n = 0; n < nfds; n++) {
            int fd = events[n].data.fd;

            if (fd == listeningSocket_) {
                // Handle new connection
                int clientSocket = accept(listeningSocket_, nullptr, nullptr);
                if (clientSocket >= 0) {
                    std::cout << "New connection accepted: " << clientSocket << "\n";
                    
                    // Add new socket to epoll
                    struct epoll_event client_ev;
                    client_ev.events = EPOLLIN;
                    client_ev.data.fd = clientSocket;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientSocket, &client_ev) == -1) {
                        close(clientSocket);
                        std::cerr << "Failed to add client to epoll" << std::endl;
                    }
                }
            } else {
                if (events[n].events & EPOLLIN) {
                    // Data available to read
                    threadPool_.enqueue([this, fd]() {
                            process_request(fd);
                    });
                }
                else{
                    std::cout << "Here 2 to: ";
                }
                
                if (events[n].events & (EPOLLHUP | EPOLLERR)) {
                    std::cout << "Here ";
                    // Socket closed or error
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                }
            }
        }
    }

    close(epoll_fd);
    close(listeningSocket_);
}



int ConnectionManager::connect_to(const std::string& destAddress, int destPort) {
    auto key = std::make_pair(destAddress, destPort);
    
    {
        std::lock_guard<std::mutex> lock(connectionMapMutex_);
        auto it = connectionMap_.find(key);
        if (it != connectionMap_.end()) {
            return it->second;
        }
    }

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
        throw std::runtime_error("Failed to connect");
    }

    {
        std::lock_guard<std::mutex> lock(connectionMapMutex_);
        connectionMap_[key] = sock;
    }
    
    // Create a lock for the new socket
    fd_lock(sock);

    return sock;
}


void ConnectionManager::send_all(int fd, const std::string& data) {
    std::lock_guard<std::mutex> lock(fd_lock(fd));
    size_t totalSent = 0;
    while (totalSent < data.size()) {
        ssize_t sent = send(fd, data.data() + totalSent, data.size() - totalSent, 0);
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

void ConnectionManager::receive_all(int fd, char* buffer, size_t size) {
    std::lock_guard<std::mutex> lock(fd_lock(fd));
    size_t totalReceived = 0;
    while (totalReceived < size) {
        ssize_t received = recv(fd, buffer + totalReceived, size - totalReceived, 0);
        if (received < 0) {
            throw std::runtime_error("Failed to receive data from socket");
        } else if (received == 0) {
            throw std::runtime_error("Connection closed unexpectedly");
        }
        totalReceived += received;
        std::cout << "Received "<< totalReceived << "/" << size <<"\n";
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
            std::cout << "Unkown request: " << header.type;
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
    int fd_to_close = -1;
    {
        std::lock_guard<std::mutex> lock(connectionMapMutex_);
        auto it = connectionMap_.find(key);
        if (it != connectionMap_.end()) {
            fd_to_close = it->second;
            connectionMap_.erase(it);
        }
    }
    
    if (fd_to_close != -1) {
        dfd_lock(fd_to_close);
        close(fd_to_close);
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
