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
#include <sys/eventfd.h> 
#include <set>
#include <shared_mutex>



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
    
    // Wake up epoll_wait
    uint64_t value = 1;
    write(wake_fd_, &value, sizeof(value));
}

void ConnectionManager::start_listening() {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        throw std::runtime_error("Failed to create epoll instance");
    }

    listeningSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listeningSocket_ < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(localPort_);
    // Instead of binding to specific IP:
    // serverAddress.sin_addr.s_addr = inet_addr(localAddress_.c_str());
    // Use INADDR_ANY to listen on all interfaces:
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    std::cout << "Binding to all interfaces on port " << localPort_ <<"\n";

    if (bind(listeningSocket_, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        close(listeningSocket_);
        throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " Failed to bind");
    }

    


    if (listen(listeningSocket_, SOMAXCONN) < 0) {
        close(listeningSocket_);
        throw std::runtime_error("Failed to listen");
    }

    // Create eventfd for waking up epoll
    wake_fd_ = eventfd(0, EFD_NONBLOCK);
    if (wake_fd_ == -1) {
        close(epoll_fd);
        throw std::runtime_error("Failed to create eventfd");
    }

    // Add eventfd to epoll
    struct epoll_event wake_ev;
    wake_ev.events = EPOLLIN;
    wake_ev.data.fd = wake_fd_;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wake_fd_, &wake_ev) == -1) {
        close(wake_fd_);
        close(epoll_fd);
        throw std::runtime_error("Failed to add eventfd to epoll");
    }


    // Add listening socket to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listeningSocket_;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listeningSocket_, &ev) == -1) {
        close(epoll_fd);
        throw std::runtime_error("Failed to add listening socket to epoll");
    }


    isListening_ = true;
    const int MAX_EVENTS = 32;
    struct epoll_event events[MAX_EVENTS];

    while (isListening_) {
        std::cout << "Listening \n";

        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            throw std::runtime_error("epoll_wait failed");
        }

        for (int n = 0; n < nfds; n++) {
            int fd = events[n].data.fd;

            if (fd == wake_fd_) {
                // Just drain the eventfd
                uint64_t value;
                read(wake_fd_, &value, sizeof(value));
                std::cout << "Stopping \n";
                continue;
            }
            else if (fd == listeningSocket_) {
                // Handle new connection
                struct sockaddr_in peer_addr;
                socklen_t peer_addr_len = sizeof(peer_addr);
                struct sockaddr_in local_addr;
                socklen_t local_addr_len = sizeof(local_addr);
    
                int clientSocket = accept(listeningSocket_, 
                            (struct sockaddr*)&peer_addr, 
                            &peer_addr_len);

                if (clientSocket >= 0) {
                    std::cout << "New connection accepted: " << clientSocket << "\n" << std::flush;

                    if (getsockname(clientSocket, (struct sockaddr*)&local_addr, &local_addr_len) == 0) {
                        std::string local_ip = inet_ntoa(local_addr.sin_addr);
                        // Use the IP directly as our interface identifier
                        // to store which interfaces are being used for outward 
                        update_inter(clientSocket, local_ip);
                    }

                    
                    // Add new socket to epoll with EPOLLONESHOT
                    struct epoll_event client_ev;
                    client_ev.events = EPOLLIN | EPOLLONESHOT;
                    client_ev.data.fd = clientSocket;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientSocket, &client_ev) == -1) {
                        close(clientSocket);
                        std::cerr << "Failed to add client to epoll" << std::endl;
                    }
                }
            } else {
                if (events[n].events & EPOLLIN) {
                    // Data available to read
                    threadPool_.enqueue([this, fd, epoll_fd]() {
                        // std::cout << "Adding task from " << fd <<"\n";
                        process_request(fd);
                        
                        // Re-arm the socket after processing
                        struct epoll_event client_ev;
                        client_ev.events = EPOLLIN | EPOLLONESHOT;
                        client_ev.data.fd = fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &client_ev);
                    });
                }
                
                if (events[n].events & (EPOLLHUP | EPOLLERR)) {
                    // Socket closed or error
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                }
            }
        }
    }

    close(wake_fd_);
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

    // Keep trying until successful - assuming all nodes must eventually come online
    // Note: This assumes no permanent node failures, only delayed starts
    int attempt = 1;
    int max_attempts = 5;
    while (attempt < max_attempts) {
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
            std::string error_msg = strerror(errno);  // Get human readable error
            close(sock);
            std::cout << "Connection attempt " << attempt << " failed to " 
                    << destAddress << ":" << destPort 
                    << " - Error: " << error_msg 
                    << " (errno: " << errno << ")\n" << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            attempt++;
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(connectionMapMutex_);
            connectionMap_[key] = sock;
        }
        
        fd_lock(sock);
        std::cout << "Connected to: " << destAddress << ":" << destPort 
                  << " after " << attempt << " attempts\n";
        return sock;
    }
    return -1;
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

void ConnectionManager::send_all(int fd, const std::string_view& data)  {
    
    std::lock_guard<std::mutex> lock(fd_lock(fd));
    size_t totalSent = 0;
    while (totalSent < data.size()) {
        ssize_t sent = send(fd, data.data() + totalSent, data.size() - totalSent, 0);
        
        if (sent < 0) {
            if (errno == EPIPE) {
                std::cerr << "Error: SIGPIPE - Peer closed the connection." << std::endl;
                throw std::runtime_error("Socket closed by peer");
            }
            std::cerr << "Error: Failed to send data to socket.\n"
                    << "Error Code: " << errno << " (" << strerror(errno) << totalSent<< " of  "<< data.size() <<")" << std::endl;
            throw std::runtime_error("Failed to send data to socket");
        }
        totalSent += sent;
        // std::cout << "Sent " << totalSent << "/" << data.size() << "\n";
    }
}

void ConnectionManager::receive_all(int fd, char* buffer, size_t size) {
    std::lock_guard<std::mutex> lock(fd_lock(fd));
    
    ssize_t received = recv(fd, buffer, size, MSG_WAITALL);
    if (received < 0) {
        throw std::runtime_error("Failed to receive data from socket");
    } else if (received == 0) {
        throw std::runtime_error("Connection closed unexpectedly while receiving");
    } else if (received != static_cast<ssize_t>(size)) {
        // This should rarely happen with MSG_WAITALL unless there's an error or interrupt
        throw std::runtime_error("Incomplete data received despite MSG_WAITALL");
    }
    
    // std::cout << "Received " << received << " bytes\n";
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
    assert(sock >= 0);
    // std::cout << "Connected to: " << destAddress<<":"<< destPort<<"\n";

    RequestHeader header = {META_REQ, 0, 0};
    std::vector<char> serializedHeader = header.serialize();
    send_all(sock, std::string_view(serializedHeader.data(), serializedHeader.size()));

    // std::cout << "Send Meta data request\n";

    // Receive the response header
    RequestHeader responseHeader;
    receive_all(sock, reinterpret_cast<char*>(&responseHeader), sizeof(RequestHeader));

    // std::cout << "Recieved meta data\n";

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

void ConnectionManager::send_piece(int clientSocket, size_t idx, const std::shared_ptr<RequestContext>& context) {

    // std::cout << "Attempting to send piece " << idx << "\n" << std::flush;
    // std::cout << "Has piece: " << fileManager_->has_piece(idx) << "\n" << std::flush;
    
    
    if (fileManager_->has_piece(idx)){
        // std::cout<<"HAVE PIECE so sending "<<idx<<"\n"<<std::flush; 
        std::string_view pieceData = fileManager_->send(idx);
        // std::cout << "Piece size: " << pieceData.size() << "\n" << std::flush;
        // std::cout << "Piece data: " << pieceData.data()[0] << "\n" << std::flush;
        RequestHeader responseHeader = {
            PIECE_RES, 
            static_cast<uint32_t>(pieceData.size()),
            static_cast<uint32_t>(idx)
        };
        std::vector<char> serializedHeader = responseHeader.serialize();
        send_all(clientSocket, std::string_view(serializedHeader.data(), serializedHeader.size()));
        send_all(clientSocket, pieceData);
        // std::cout<<"Piece Sent\n"; 
    } else {
        // std::cout<<"PIECE Not found so queeing task " << idx <<std::flush; 
        context->remainingPieces.insert(idx);
        fileManager_->register_piece_callback(idx, 
            [context, idx = idx](size_t) {
                // std::cout<<"Callback wake up! found piece\n";
                // what happens if the node becomes availabel between the time I hccked
                // and between the time I call registore that is definelty posisbly
                // a lsot wakeup type of problem could be avoided by filemanager
                // calling all remaining callbacks once it has all the list of pieces  
                std::lock_guard<std::mutex> lock(context->mutex);
                context->availablePieces.insert(idx);
                context->cv.notify_one();
            });
    }
   
}


void ConnectionManager::process_piece_request(int clientSocket, const RequestHeader& header) {
    if (!fileManager_) {
        throw std::runtime_error("Cannot serve piece request: no FileManager available");
    }

    std::vector<char> requestBuffer(header.payloadSize);
    receive_all(clientSocket, requestBuffer.data(), header.payloadSize);
    PieceRequest request = PieceRequest::deserialize(requestBuffer);

    // First check if we can use the interface
    if (!acquire_inter(clientSocket)) {
        // Interface is busy, send BUSY_RES
        RequestHeader busyHeader = {BUSY_RES, 0, 0};
        std::vector<char> serializedHeader = busyHeader.serialize();
        send_all(clientSocket, std::string_view(serializedHeader.data(), serializedHeader.size()));
        return;
    }

    // if interface acquisition succeds setup automatica unlcoker
    InterfaceGuard guard(*this, clientSocket);

    if (fileManager_->available_pieces() == 0) {
        // Interface is busy, send BUSY_RES
        RequestHeader busyHeader = {NOT_AVAIL_RES, 0, 0};
        std::vector<char> serializedHeader = busyHeader.serialize();
        send_all(clientSocket, std::string_view(serializedHeader.data(), serializedHeader.size()));
        return;
    }



    // Create context for this request
    auto context = std::make_shared<RequestContext>();

    // Process single piece request
    if (request.types & SINGLE_PIECE) {
        send_piece(clientSocket, request.pieceIndex, context);
    }

    // Process range requests
    if (request.types & PIECE_RANGE) {
        for (const auto& range : request.ranges) {
            for (size_t idx = range.first; idx <= range.second; idx++) {
                send_piece(clientSocket, idx, context);
            }
        }
    }

    // Process piece list
    if (request.types & PIECE_LIST) {
        for (size_t idx : request.pieces) {
            send_piece(clientSocket, idx, context);
        }
    }

    wait_for_queue(clientSocket, context);
    
}

void ConnectionManager::wait_for_queue(int clientSocket, const std::shared_ptr<RequestContext>& context) {
    // std::cout << "Waiting for " << context->remainingPieces.size() << " pieces\n" << std::flush;
    std::unique_lock<std::mutex> lock(context->mutex);
    while (!context->remainingPieces.empty()) {
        if (context->availablePieces.empty()) {
            // std::cout << "No pieces available, waiting...\n" << std::flush;
            context->cv.wait(lock);
            // std::cout << "Woke up, available pieces: " << context->availablePieces.size() << "\n" << std::flush;
        }

        auto available = std::move(context->availablePieces);
        lock.unlock();

        for (auto idx : available) {
            send_piece(clientSocket, idx, context);
            // std::cout << "Sent queued piece " << idx << "\n" << std::flush;
            
            lock.lock();
            context->remainingPieces.erase(idx);
            lock.unlock();
        }
        lock.lock();
    }
    std::cout << "All pieces sent\n" << std::flush;
}

void ConnectionManager::process_meta_request(int clientSocket, const RequestHeader& header) {
    (void) header;  

    if (!fileManager_) {
        throw std::runtime_error("Cannot serve metadata request: no FileManager available");
    }

    std::cout << "Got meta data request\n";

    std::string serializedData = fileManager_->get_metadata().serialize();
    RequestHeader responseHeader = {META_RES, static_cast<uint32_t>(serializedData.size()),0};
    std::vector<char> serializedHeader = responseHeader.serialize();

    send_all(clientSocket, std::string_view(serializedHeader.data(), serializedHeader.size()));
    std::cout << "SENT META header\n";
    send_all(clientSocket, std::string_view(serializedData)); 
    std::cout << "SENT META data\n";
}


void ConnectionManager::request_pieces(const std::string& destAddress, int destPort, 
                                     size_t single_piece,
                                     const std::vector<std::pair<size_t, size_t>>& ranges,
                                     const std::vector<size_t>& piece_list) {
    if (!fileManager_) {
        throw std::runtime_error("No FileManager available for receiving pieces");
    }

    int sock = connect_to(destAddress, destPort);
    if (sock < 0){
        throw std::runtime_error("NOT_AVAIL");
    }

    // Prepare combined piece request
    PieceRequest request;
    request.types = 0;
    
    if (single_piece != (size_t)-1) {
        request.types |= SINGLE_PIECE;
        request.pieceIndex = single_piece;
    }
    if (!ranges.empty()) {
        request.types |= PIECE_RANGE;
        request.ranges = ranges;
    }
    if (!piece_list.empty()) {
        request.types |= PIECE_LIST;
        request.pieces = piece_list;
    }

    std::vector<char> serializedRequest = request.serialize();
    RequestHeader header = {PIECE_REQ, static_cast<uint32_t>(serializedRequest.size()), 0};
    std::vector<char> serializedHeader = header.serialize();
    
    // Send the request
    send_all(sock, std::string_view(serializedHeader.data(), serializedHeader.size()));
    send_all(sock, std::string_view(serializedRequest.data(), serializedRequest.size()));

    // Calculate total expected pieces
    size_t total_pieces = 0;
    if (request.types & SINGLE_PIECE) total_pieces++;
    if (request.types & PIECE_RANGE) {
        for (const auto& range : ranges) {
            total_pieces += range.second - range.first + 1;
        }
    }
    if (request.types & PIECE_LIST) total_pieces += piece_list.size();

    // Receive all pieces
    for (size_t i = 0; i < total_pieces; i++) {
        RequestHeader responseHeader;
        receive_all(sock, reinterpret_cast<char*>(&responseHeader), sizeof(RequestHeader));

        // Check if interface is busy
        if (responseHeader.type == BUSY_RES) {
            throw std::runtime_error("BUSY");  // Special error message for busy case
        }  

        if (responseHeader.type == NOT_AVAIL_RES) {
            throw std::runtime_error("NOT_AVAIL");  // Special error message for not available case
        }      
        
        if (responseHeader.type != PIECE_RES) {
            throw std::runtime_error("Unexpected response type for piece request");
        }

        // Size check
        assert(responseHeader.payloadSize == fileManager_->piece_size);

        // Now we use the piece index from the response header
        if (!fileManager_->has_piece(responseHeader.pieceIndex)) {
            size_t buffer_size;
            char* write_buffer = fileManager_->get_piece_buffer(responseHeader.pieceIndex, buffer_size);
            assert(write_buffer != nullptr);
            receive_all(sock, write_buffer, responseHeader.payloadSize);
            fileManager_->update_piece_status(responseHeader.pieceIndex);
            // if (responseHeader.pieceIndex == 0){
            //     std::cout<<"BANG BANG address "<< static_cast<const void*>(write_buffer) <<"\n"<<std::flush;
            // }
            // std::cout<<"HERE "<< write_buffer[0]<< " THE BUFFER\n"<<std::flush;
        } else {
            // Skip the piece if we already have it
            std::vector<char> dummy_buffer(responseHeader.payloadSize);
            receive_all(sock, dummy_buffer.data(), responseHeader.payloadSize);
        }
        // std::cout <<"Recieved "<< i<< " of " << total_pieces <<  "\n"<<std::flush;
    }
}


bool ConnectionManager::acquire_inter(int socket_fd) {
    // Find the interface for this socket
    std::string interface_name;
    {
        std::shared_lock<std::shared_mutex> socket_lock(socket_map_mutex_);
        auto socket_it = socket_to_interface_.find(socket_fd);
        assert(socket_it != socket_to_interface_.end() && "Socket must be associated with an interface");
        interface_name = socket_it->second;
    }
    
    // Now look up the interface state
    std::shared_lock<std::shared_mutex> interface_lock(interface_map_mutex_);
    auto interface_it = interface_states_.find(interface_name);
    assert(interface_it != interface_states_.end() && "Interface must exist in interface states");

    auto& interface_state = interface_it->second;
    
    // Try to mark interface as busy
    bool expected = false;
    if (!interface_state->is_busy.compare_exchange_strong(expected, true)) {
        return false; // Interface is already busy
    }
    
    interface_state->busy_socket.store(socket_fd);
    return true;
}

void ConnectionManager::release_inter(int socket_fd) {
    // Get interface name for this socket
    std::string interface_name;
    {
        std::shared_lock<std::shared_mutex> socket_lock(socket_map_mutex_);
        auto socket_it = socket_to_interface_.find(socket_fd);
        assert(socket_it != socket_to_interface_.end() && "Socket must be associated with an interface");
        interface_name = socket_it->second;
    }
    
    // Access interface state
    std::shared_lock<std::shared_mutex> interface_lock(interface_map_mutex_);
    auto interface_it = interface_states_.find(interface_name);
    assert(interface_it != interface_states_.end() && "Interface must exist in interface states");

    auto& interface_state = interface_it->second;
    
    // Only allow the socket that acquired the interface to release it
    assert (interface_state->busy_socket.load() == socket_fd);
    
    interface_state->busy_socket.store(-1);
    interface_state->is_busy.store(false);
}


void ConnectionManager::update_inter(int socket_fd, const std::string& inter) {
   
   assert(!inter.empty() && "Must find valid interface for peer IP");
   
   // Update socket to interface mapping
   {
       std::unique_lock<std::shared_mutex> socket_lock(socket_map_mutex_);
       socket_to_interface_[socket_fd] = inter;
   }
   
   // Update interface state
   {
       std::unique_lock<std::shared_mutex> interface_lock(interface_map_mutex_);
       auto& interface_state = interface_states_[inter];
       if (!interface_state) {
           interface_state = std::make_unique<InterfaceState>();
           interface_state->interface_name = inter;
       }
       
       std::lock_guard<std::mutex> state_lock(interface_state->state_mutex);
       interface_state->associated_sockets.insert(socket_fd);
   }
}