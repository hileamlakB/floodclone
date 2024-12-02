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

std::string FloodClone::find_immediate_neighbor(const std::string& target_node) {
    // For target_node = src, we want to look at our own routes to src
    auto routes_it = network_map.find(args.node_name); // Look up our routes
    if (routes_it == network_map.end()) {
        throw std::runtime_error("No routes found for " + args.node_name);
    }

    auto target_routes = routes_it->second.find(target_node);
    if (target_routes == routes_it->second.end()) {
        throw std::runtime_error("No route to " + target_node);
    }

    // Error if multiple routes exist
    if (target_routes->second.size() > 1) {
        throw std::runtime_error("Multiple routes to " + target_node + " not yet supported");
    }

    const auto& path = target_routes->second[0].path;
    
    // If path length is 1, request directly from target
    if (path.size() <= 1) {
        return target_node;
    }

    // Otherwise, return the first node in our path to target
    return path[0];  // This will be d1 for d2->src path
}

std::string FloodClone::get_ip(const std::string& target_node) {
    // Find how target_node connects to us
    auto src_it = network_map.find(target_node);
    if (src_it == network_map.end()) {
        throw std::runtime_error("No route from " + target_node + " to " + args.node_name);
    }

    auto dest_it = src_it->second.find(args.node_name);
    if (dest_it == src_it->second.end()) {  // Fixed: compare with src_it instead of dest_it
        throw std::runtime_error("No route from " + target_node + " to " + args.node_name);
    }

    // Get the interface target uses to reach us
    assert(dest_it->second.size() == 1 && "Multiple routes not yet supported");
    std::string target_interface = dest_it->second[0].interface;

    // Lookup the IP for this interface
    auto node_it = ip_map.find(target_node);
    if (node_it == ip_map.end()) {
        throw std::runtime_error("No IP found for node: " + target_node);
    }

    auto iface_it = node_it->second.find(target_interface);
    if (iface_it == node_it->second.end()) {
        throw std::runtime_error("No IP found for interface: " + target_interface);
    }

    return iface_it->second;
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
        listen_thread = std::thread([this]() {
            connection_manager->start_listening();
        });

        std::cout << "Destination: Started listening thread.\n";

        // First find the closest node we should request from
        std::string request_node = find_immediate_neighbor(args.src_name);
        std::string target_ip = get_ip(request_node);
        
        std::cout << "Destination: Requesting from " << request_node 
                  << " (" << target_ip << ")\n";

        auto metadata = connection_manager->request_metadata(target_ip, LISTEN_PORT);
        
        file_manager = std::make_unique<FileManager>(
            args.file_path, 0, my_ip,
            args.pieces_dir, &thread_pool, false, &metadata
        );

        std::cout << "Destination: FileManager created. Num pieces: " 
                    << metadata.numPieces << "\n";

        connection_manager->set_file_manager(*file_manager);

        // Request all pieces in one range
        connection_manager->request_pieces(
            target_ip, LISTEN_PORT,
            -1,  // no single piece
            {{0, metadata.numPieces - 1}},  // request full range
            {}   // no specific list
        );
       

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
    addr.sin_addr.s_addr = inet_addr(my_ip.c_str());
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

        std::string peer_ip = get_ip(node);
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        sockaddr_in peer_addr;
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_addr.s_addr = inet_addr(peer_ip.c_str());
        peer_addr.sin_port = htons(COMPLETION_PORT);
        std::cout << "Sending Notification: " <<  peer_ip << ":" << COMPLETION_PORT << "\n" << std::flush;


        if (connect(sock, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) >= 0) {
            std::cout << "Notified completion to " << node << "\n";
        }
        close(sock);
    }
}