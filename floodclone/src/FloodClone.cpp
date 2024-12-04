#include "FloodClone.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <arpa/inet.h>
#include <sys/socket.h>

constexpr int LISTEN_PORT = 9089;

FloodClone::FloodClone(const Arguments& args)
    : thread_pool(4), args(args), 
      start_time(std::chrono::system_clock::now())
{
    setup_net_info();
    setup_node();
}
void FloodClone::setup_net_info() {
    // Setup IP mappings
    // Input format: {"d1": [["d1-eth0", "10.0.0.1"]], "src": [["src-eth0", "10.0.0.2"]]}
    for (const auto& [node, interfaces] : args.ip_map.items()) {
        for (const auto& interface_info : interfaces) {
            std::string iface = interface_info[0];
            std::string ip = interface_info[1];
            ip_map[node][iface] = ip;
        }
    }
    
    // Setup network topology mappings
    // Input format: {"d1": {"src": [["d1-eth0", 1, ["d1"]]]}, "src": {"d1": [["src-eth0", 1, ["src"]]]}}
    for (const auto& [src_node, destinations] : args.network_info.items()) {
        for (const auto& [dest_node, routes] : destinations.items()) {
            for (const auto& route : routes) {
                RouteInfo route_info{
                    route[0],           // interface
                    route[1],           // hop_count
                    route[2]            // path
                };
                network_map[src_node][dest_node].push_back(route_info);
            }
        }
    }
}

std::vector<std::string> FloodClone::find_immediate_neighbors() {
    std::vector<std::string> neighbors;
    
    auto routes_it = network_map.find(args.node_name);
    if (routes_it == network_map.end()) {
        throw std::runtime_error("No routes found for " + args.node_name);
    }

    // Look at all destinations from our node
    for (const auto& [dest_node, routes] : routes_it->second) {
        // Check for duplicate interfaces
        std::set<std::string> interfaces;
        for (const auto& route : routes) {
            if (!interfaces.insert(route.interface).second) {
                std::cout << "Multiple routes using same interface to " + dest_node + " not yet supported\n";
            }
        }
        
        // If any route to this dest is one hop, add it
        for (const auto& route : routes) {
            if (route.hop_count == 1) {
                neighbors.push_back(dest_node);
                break;  // Found one one-hop path to this dest, no need to check others
            }
        }
    }

    if (neighbors.empty()) {
        throw std::runtime_error("No one-hop neighbors found");
    }

    return neighbors;
}

std::vector<ConnectionOption> FloodClone::get_ip(const std::string& target_node) {
    
    std::vector<ConnectionOption> connection_options;

    // Look up routes from us to the target node
    auto routes_it = network_map.find(args.node_name);
    if (routes_it == network_map.end()) {
        throw std::runtime_error("No routes found from " + args.node_name);
    }

    auto target_routes = routes_it->second.find(target_node);
    if (target_routes == routes_it->second.end()) {
        throw std::runtime_error("No routes found to " + target_node);
    }

    // For each route we have to reach the target
    for (const auto& route : target_routes->second) {
        
        // Look up the target's IP that corresponds to this route
        auto node_it = ip_map.find(target_node);
        if (node_it == ip_map.end()) {
            continue;  
        }

        // Find the target's IP for the interface on their end
        for (const auto& [target_iface, target_ip] : node_it->second) {        
            connection_options.push_back({
                .target_ip = target_ip,
                .local_interface = route.interface
            });
        }
    }

    if (connection_options.empty()) {
        throw std::runtime_error("No valid connection options found to " + target_node);
    }

    return connection_options;
}
void FloodClone::setup_node() {
    total_nodes_ = network_map.size();
    std::cout << "Total nodes "<< total_nodes_ << "\n";

    auto node_ips = ip_map[args.node_name];
    if (node_ips.empty()) {
        throw std::runtime_error("No IPs found for node: " + args.node_name);
    }
    my_ip = node_ips.begin()->second;  // Get first interface's IP
    std::cout << args.node_name << " using IP " << my_ip << std::endl;
    
    if (args.mode == "source") {
        total_nodes_ -= 1; // source doens't expect notificaiton from itself
        file_manager = std::make_unique<FileManager>(
            args.file_path, 0, my_ip, 
            args.pieces_dir, &thread_pool, true, nullptr
        );
        std::cout << "Source: FileManager created and file split into pieces.\n";

        connection_manager = std::make_unique<ConnectionManager>(
            my_ip, LISTEN_PORT, thread_pool, *file_manager
        );
    } else {

        total_nodes_ -= 2; // destination doesnt' expect notifiation from src and itself
        connection_manager = std::make_unique<ConnectionManager>(
            my_ip, LISTEN_PORT, thread_pool
        );
    }
}

void FloodClone::start() {
    

    setup_completion();

    std::thread listen_thread;
    
    if (args.mode == "source") {
        record_time();
        listen_thread = std::thread([this]() {
            connection_manager->start_listening();
        });

        //wait for all the nodes to complete
        std::unique_lock<std::mutex> lock(node_mtx);
        while (completed_nodes_ < total_nodes_) {
            node_change.wait(lock);
        }

        std::cout << "Notification found from " << total_nodes_ << " nodes\n"<<std::flush;

        connection_manager->stop_listening();

        if (listen_thread.joinable()) {
            listen_thread.join();
        }

        std::cout << "Source: Finished listening\n"<<std::flush;
       
    } 

    else{
        try {
        

        std::cout << "Destination: Started listening thread.\n";

        // First find the closest node we should request from
        auto neighbors = find_immediate_neighbors();
        

        std::vector<ConnectionOption> ips = get_ip(neighbors[0]);
        for (auto &ip: ips){
            std::cout<< "Found IP " << ip.target_ip << "on interface " << ip.local_interface << "\n" << std::flush;
        }
        
        std::cout << "Destination: Requesting from " << neighbors[0] 
                  << " (" << ips[0].target_ip << ")\n";

        // I will need to update request_metadata, connect_to and others to be able to 
        // reconinze when there are mulitple interfaces
        auto metadata = connection_manager->request_metadata(ips[0].target_ip, LISTEN_PORT);
        
        file_manager = std::make_unique<FileManager>(
            args.file_path, 0, my_ip,
            args.pieces_dir, &thread_pool, false, &metadata
        );

        std::cout << "Destination: FileManager created. Num pieces: " 
                    << metadata.numPieces << "\n";

        connection_manager->set_file_manager(*file_manager);

        // set detsination to listening state only once it has metadata so 
        // that it can also provide mteadata once other request
        listen_thread = std::thread([this]() {
            connection_manager->start_listening();
        });

        
        while (true) {
            for (const auto& neighbor : neighbors) {
                try {
                    std::vector<ConnectionOption> neighbor_ips = get_ip(neighbor);
                    std::cout << "Attempting transfer from neighbor: " << neighbor 
                            << " (" << neighbor_ips[0].target_ip << ")\n";

                    connection_manager->request_pieces(
                        neighbor_ips[0].target_ip, LISTEN_PORT,
                        -1,  // no single piece
                        {{0, metadata.numPieces - 1}},  // request full range
                        {}   // no specific list
                    );
                    
                    // If we get here, transfer was successful
                    std::cout << "Transfer completed successfully from " << neighbor << "\n";
                    goto transfer_complete;  // Break out of both loops

                } catch (const std::runtime_error& e) {
                    std::string error = e.what();
                    if (error == "BUSY") {
                        std::cout << "Neighbor " << neighbor << " is busy, trying next neighbor\n";
                        continue;
                    }
                    // For non-BUSY errors, rethrow
                    throw;
                }
            }
            // If we get here, all neighbors were busy - sleep before retrying
            std::cout << "All neighbors busy, waiting before retry...\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        transfer_complete:
            thread_pool.wait();
            std::cout << "Client: Received all pieces\n";

       

        thread_pool.wait();
        std::cout << "Client: Received all pieces\n";

        // Verify all pieces
        for (size_t i = 0; i < metadata.numPieces; i++) {
            if (!file_manager->has_piece(i)) {
                throw std::runtime_error("Missing piece " + std::to_string(i));
            }
        }

        file_manager->reconstruct();
        record_time();
        
        notify_completion();
        
        std::unique_lock<std::mutex> lock(node_mtx);
        while (completed_nodes_ < total_nodes_) {
            node_change.wait(lock);
        }
        std::cout << "Finished completion "<< completed_nodes_ << " of " << total_nodes_ << std::flush;

        file_manager->clean_up();
        connection_manager->stop_listening();
        if (listen_thread.joinable()) {
            listen_thread.join();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error in " << args.node_name << " at "
                 << __FILE__ << ":" << __LINE__ << ": " << e.what() << std::endl;
        connection_manager->stop_listening();
        if (listen_thread.joinable()) {
            listen_thread.join();
        }
        throw;
    }

    }

    is_listening_ = false;
    if (completion_thread_.joinable()) {
        completion_thread_.join();
    }
    close(completion_socket_);  


    thread_pool.wait();


}

void FloodClone::record_time() {
   
    end_time = std::chrono::system_clock::now(); 
    auto start_micros = std::chrono::duration_cast<std::chrono::microseconds>(
        start_time.time_since_epoch()
    ).count();

    auto end_micros = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time.time_since_epoch()
    ).count();

    std::ofstream timestamp_file(args.timestamp_file);
    timestamp_file << start_micros << "\n";
    timestamp_file << end_micros;
}

Arguments parse_args(int argc, char* argv[]) {
    Arguments args;
    for(int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if(arg == "--mode") args.mode = argv[++i];
        else if(arg == "--node-name") args.node_name = argv[++i];
        else if(arg == "--src-name") args.src_name = argv[++i];
        else if(arg == "--file") args.file_path = argv[++i];
        else if(arg == "--pieces-dir") args.pieces_dir = argv[++i];
        else if(arg == "--timestamp-file") args.timestamp_file = argv[++i];
        else if(arg == "--network-info") args.network_info = nlohmann::json::parse(argv[++i]);
        else if(arg == "--ip-map") args.ip_map = nlohmann::json::parse(argv[++i]);
    }
    return args;
}


void FloodClone::setup_completion() {
    completion_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (completion_socket_ < 0) {
        throw std::runtime_error("Failed to create completion socket");
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // listen on any port
    addr.sin_port = htons(COMPLETION_PORT);
    

    if (bind(completion_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        throw std::runtime_error("Failed to bind completion socket");
    }

    if (listen(completion_socket_, SOMAXCONN) < 0) {
        throw std::runtime_error("Failed to listen on completion socket");
    }

    completion_thread_ = std::thread(&FloodClone::listen_for_completion, this);
}

void FloodClone::listen_for_completion() {
    while (is_listening_) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(completion_socket_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

       

        {
            std::lock_guard<std::mutex> lock(node_mtx);
            completed_nodes_++;
            std::cout << "Node completed. Count: " << completed_nodes_ << "/" 
                      << total_nodes_ << "\n" << std::flush;
            node_change.notify_all();
        }

        close(client_fd);
    }
}

void FloodClone::notify_completion() {
    
    for (const auto& [node, routes] : network_map) {
        if (node == args.node_name) continue;  // Skip self
        

        std::cout << "Sending Notification to: " <<  node << "\n" << std::flush;

        std::vector<ConnectionOption>  peer_ips = get_ip(node);
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        sockaddr_in peer_addr;
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_addr.s_addr = inet_addr(peer_ips[0].target_ip.c_str());
        peer_addr.sin_port = htons(COMPLETION_PORT);
        std::cout << "Sending Notification: " <<  peer_ips[0].target_ip << ":" << COMPLETION_PORT << "\n" << std::flush;


        if (connect(sock, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) >= 0) {
            std::cout << "Notified completion to " << node << "\n";
        }
        close(sock);
    }
}