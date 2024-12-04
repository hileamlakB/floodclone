#include "ThreadPool.h"
#include "FileManager.h"
#include "ConnectionManager.h"
#include "FloodClone.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>

bool compare_files(const std::string& file_path1, const std::string& file_path2) {
    std::ifstream file1(file_path1, std::ios::binary);
    if (!file1.is_open()) {
        std::cerr << "Error opening file: " << file_path1 << " - " << std::strerror(errno) << std::endl;
        throw std::runtime_error("Cannot open file: " + file_path1);
    }

    std::ifstream file2(file_path2, std::ios::binary);
    if (!file2.is_open()) {
        std::cerr << "Error opening file: " << file_path2 << " - " << std::strerror(errno) << std::endl;
        throw std::runtime_error("Cannot open file: " + file_path2);
    }

    // Proceed with file comparison
    return std::equal(
        std::istreambuf_iterator<char>(file1.rdbuf()),
        std::istreambuf_iterator<char>(),
        std::istreambuf_iterator<char>(file2.rdbuf())
    );
}

// Shared pointers to track managers
static std::shared_ptr<ConnectionManager> server_manager;
static std::shared_ptr<ConnectionManager> client_manager;


// Create test setup function
Arguments create_test_args(bool is_server) {
    Arguments args;
    
    // Basic args
    args.mode = is_server ? "source" : "destination";
    args.node_name = is_server ? "server" : "client";
    args.src_name = "server";  // server is always source
    args.file_path = is_server ? "tests/test_file.txt" : "tests/received_test_file.txt";
    args.pieces_dir = is_server ? "tests/sender_pieces" : "tests/receiver_pieces";
    args.timestamp_file = is_server ? "server_completion_time" : "client_completion_time";

    // Network topology for test setup
    args.network_info = nlohmann::json::parse(
        R"({
            "server": {"client": [["server-eth0", 1, []]]},
            "client": {"server": [["client-eth0", 1, []]]}
        })"
    );

    // IP mappings
    args.ip_map = nlohmann::json::parse(
        R"({
            "server": [["server-eth0", "127.0.0.1"]],
            "client": [["client-eth0", "127.0.0.1"]]
        })"
    );

    return args;
}

void run_server() {
    auto args = create_test_args(true);
    FloodClone flood_clone(args, 9085);  // Server on 9085
    flood_clone.start();
}

void run_client() {
    try {
        auto args = create_test_args(false);
        FloodClone flood_clone(args, 8080);  // Client on 8080
        flood_clone.start();

        // Just verify the result
        if (compare_files("tests/test_file.txt", 
                         "tests/received_test_file.txt")) {
            std::cout << "Test passed: Files match.\n";
        } else {
            std::cerr << "Test failed: Files don't match.\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        throw;
    }
}


#ifdef TESTING
int main() {
    

    // Start server in separate thread
    std::thread server_thread([]() {
        try {
            run_server();
        } catch (const std::exception& e) {
            std::cerr << "Server error: " << e.what() << std::endl;
        }
    });

    std::cout << "Server creation complete\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Creating client\n";
    
    // Run client in main thread
    try {
        run_client();
    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
    }

    std::cout << "Client finished, stopping connections...\n";

    // Stop both managers
    if (client_manager) {
        client_manager->stop_listening();
    }
    if (server_manager) {
        server_manager->stop_listening();
    }

    // Wait for server thread to finish
    server_thread.join();
    
    std::cout << "Clean shutdown complete\n";
    return 0;
}
#endif
