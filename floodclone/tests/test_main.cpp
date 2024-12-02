#include "ThreadPool.h"
#include "FileManager.h"
#include "ConnectionManager.h"
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

void run_server(ThreadPool& threadPool) {
    // Create the sender FileManager
    FileManager senderFileManager("tests/test_file.txt", 0, "127.0.0.1", 
                                "tests/sender_pieces", &threadPool, true, nullptr);
    std::cout << "Server: FileManager created and file split into pieces.\n";

    // Create and store server
    server_manager = std::make_shared<ConnectionManager>("127.0.0.1", 9085, 
                                                       threadPool, senderFileManager);
    server_manager->start_listening();
}

void run_client(ThreadPool& threadPool) {
    try {
        // Create and store client
        client_manager = std::make_shared<ConnectionManager>("127.0.0.1", 8080, threadPool);
        
        // Get metadata from server
        FileMetaData metadata = client_manager->request_metadata("127.0.0.1", 9085);
        
        // Create receiver FileManager with received metadata
        FileManager receiverFileManager("tests/received_test_file.txt", 0, "127.0.0.1", "tests/receiver_pieces", 
                                     &threadPool, false, &metadata);
        std::cout << "Client: FileManager created from metadata. Num pieces: " 
                  << metadata.numPieces << "\n";

        client_manager->set_file_manager(receiverFileManager);

        // Request all pieces in one range
        client_manager->request_pieces(
            "127.0.0.1", 
            9085,
            -1,  // no single piece
            {{0, metadata.numPieces - 1}},  // request full range
            {}   // no specific list
        );
        std::cout << "Client: Received all pieces\n";

        // Verify all pieces received
        for (size_t i = 0; i < metadata.numPieces; ++i) {
            if (!receiverFileManager.has_piece(i)) {
                std::cerr << "Error: Piece " << i << " missing after completion\n";
            }
        }

        // Reconstruct the file
        receiverFileManager.reconstruct();
        std::cout << "Client: File reconstruction complete.\n";

        // Verify the reconstruction
        // if (compare_files("tests/test_file.txt", 
        //                  "tests/received_test_file.txt")) {
        //     std::cout << "Test passed: Reconstructed file matches original.\n";
        // } else {
        //     std::cerr << "Test failed: Reconstructed file does not match original.\n";
        // }

    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        throw;
    }
}


#ifdef TESTING
int main() {
    ThreadPool threadPool(4);

    // Start server in separate thread
    std::thread server_thread([&threadPool]() {
        try {
            run_server(threadPool);
        } catch (const std::exception& e) {
            std::cerr << "Server error: " << e.what() << std::endl;
        }
    });

    std::cout << "Server creation complete\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Creating client\n";
    
    // Run client in main thread
    try {
        run_client(threadPool);
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
