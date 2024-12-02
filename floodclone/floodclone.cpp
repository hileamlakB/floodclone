
#include "FloodClone.h"
#include <iostream>





int main(int argc, char* argv[]) {
    auto args = parse_args(argc, argv);

    std::cout << "Mode: " << args.mode << std::endl;
    std::cout << "Node name: " << args.node_name << std::endl;
    std::cout << "Source name: " << args.src_name << std::endl;
    std::cout << "File path: " << args.file_path << std::endl;
    std::cout << "Pieces directory: " << args.pieces_dir << std::endl;
    std::cout << "Timestamp file: " << args.timestamp_file << std::endl;
    std::cout << "Network info: " << args.network_info.dump(2) << std::endl;
    std::cout << "Ip Map: " << args.ip_map.dump(2) << std::endl;

    try {
        std::cout << "Starting " << args.mode << " node: " << args.node_name << std::endl;
        
        FloodClone flood_clone(args);
        flood_clone.start();
        
        std::cout << "Node " << args.node_name << " completed successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}