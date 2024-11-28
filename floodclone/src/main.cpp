#include "ThreadPool.h"
#include "FileManager.h"
#include "ConnectionManager.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>


bool compare_files(const std::string& file_path1, const std::string& file_path2) {
    std::ifstream file1(file_path1, std::ios::binary);
    std::ifstream file2(file_path2, std::ios::binary);

    if (!file1.is_open() || !file2.is_open()) {
        throw std::runtime_error("Cannot open one of the files for comparison.");
    }

    return std::equal(
        std::istreambuf_iterator<char>(file1.rdbuf()),
        std::istreambuf_iterator<char>(),
        std::istreambuf_iterator<char>(file2.rdbuf())
    );
}

void run_server(ThreadPool& threadPool) {
    // Create the sender FileManager
    FileManager senderFileManager("tests/test_file.txt", 0, "127.0.0.1", 
                                "tests/sender_pieces", &threadPool, true, nullptr);
    std::cout << "Server: FileManager created and file split into pieces.\n";

    // Create and start the server
    ConnectionManager server("127.0.0.1", 9085, threadPool, senderFileManager);
    server.start_listening();  // This will run until server is stopped
}

void run_client(ThreadPool& threadPool) {
    try {
        // First, get metadata from server
        ConnectionManager client("127.0.0.1", 8080, threadPool);
        FileMetaData metadata = client.request_metadata("127.0.0.1", 9085);
        
        // Create receiver FileManager with received metadata
        FileManager receiverFileManager("", 0, "127.0.0.1", "tests/receiver_pieces", 
                                     &threadPool, false, &metadata);
        std::cout << "Client: FileManager created from metadata. Nume of pieces " << metadata.numPieces <<" \n";



        // Request each piece
        for (size_t i = 0; i < metadata.numPieces; ++i) {
            std::cout << "Aboout to request piece " << i << "-"<< receiverFileManager.has_piece(i)<<" \n";
            if (!receiverFileManager.has_piece(i)) {
                std::string piece_data = client.request_piece("127.0.0.1", 9085, i);
                receiverFileManager.receive(piece_data, i);
                std::cout << "Client: Received piece " << i << std::endl;
            }
        }

        // Wait for all receive operations to complete
        threadPool.wait();

        // Verify all pieces received
        for (size_t i = 0; i < metadata.numPieces; ++i) {
            if (!receiverFileManager.has_piece(i)) {
                std::cerr << "Error: Piece " << i << " should be available after receiving is complete\n";
            }
        }

        // Reconstruct the file
        receiverFileManager.reconstruct();
        std::cout << "Client: File reconstruction complete.\n";

        // Verify the reconstruction
        if (compare_files("tests/test_file.txt", 
                         "tests/receiver_pieces/reconstructed_test_file.txt")) {
            std::cout << "Test passed: Reconstructed file matches the original.\n";
        } else {
            std::cerr << "Test failed: Reconstructed file does not match the original.\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
    }
}

int main() {
    ThreadPool threadPool(4);  // Single thread pool for both client and server

    // Start server in separate thread
    std::thread server_thread(run_server, std::ref(threadPool));

    std::cout << "Server creation complete\n";
    
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "Creating client\n";
    
    // Run client
    std::thread client_thread(run_client, std::ref(threadPool));

    std::cout << "Client Creation complete\n";
    
    // Wait for client to finish
    client_thread.join();
    
    // TODO: Add proper server shutdown mechanism
    // For now, we can just terminate the program
    
    return 0;
}