
#include "ThreadPool.h"
#include "FileManager.h"
#include <iostream>
#include <string>
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

bool compare_files(const std::string& file_path1, const std::string& file_path2) {
    std::ifstream file1(file_path1, std::ios::binary);
    std::ifstream file2(file_path2, std::ios::binary);

    // Ensure both files opened correctly
    if (!file1.is_open() || !file2.is_open()) {
        throw std::runtime_error("Cannot open one of the files for comparison.");
    }

    // Use iterators to compare contents directly
    return std::equal(
        std::istreambuf_iterator<char>(file1.rdbuf()),
        std::istreambuf_iterator<char>(),
        std::istreambuf_iterator<char>(file2.rdbuf())
    );
}
int main() {
    ThreadPool senderthreadPool(4);

    // Create the sender FileManager
    FileManager senderFileManager("tests/test_file.txt", 0, "127.0.0.1", "tests/sender_pieces", &senderthreadPool, true, "");
    std::cout << "Sender FileManager created and file split into pieces.\n";

    // Wait for all tasks in thread pool to complete
    senderthreadPool.join();

    // Save metadata to file for the receiver
    std::string metadata_file_path = senderFileManager.save_metadata();
    std::cout << "Sender metadata saved to " << metadata_file_path << "\n";

    ThreadPool recieverthreadpool(4);


    // Create the receiver FileManager
    FileManager receiverFileManager("", 0, "127.0.0.1", "tests/receiver_pieces", &recieverthreadpool, false, metadata_file_path);
    std::cout << "Receiver FileManager created from metadata.\n";


     for (size_t i = 0; i < senderFileManager.get_metadata().numPieces; ++i) {
        if (receiverFileManager.has_piece(i)) {
            std::cerr << "Error: Piece " << i << " should initially be unavailable.\n";
        }

        if (!senderFileManager.has_piece(i)) {
            std::cerr << "Error: Piece " << i << " should be avialable at the sender.\n";
        }
    }

    // Simulate piece transfer from sender to receiver
    for (size_t i = 0; i < senderFileManager.get_metadata().numPieces; ++i) {
        std::string piece_data = senderFileManager.send(i);
        receiverFileManager.receive(piece_data, i);
    }

    recieverthreadpool.wait();

    for (size_t i = 0; i < senderFileManager.get_metadata().numPieces; ++i) {
        if (!receiverFileManager.has_piece(i)) {
            std::cerr << "Error: Piece " << i << " should be available after reciving is complete\n";
        }
    }

    // Reconstruct the file on the receiver side
    receiverFileManager.reconstruct();

   

    // Perform a one-to-one file comparison
    if (compare_files("tests/test_file.txt", "tests/receiver_pieces/reconstructed_test_file.txt")) {
        std::cout << "Test passed: Reconstructed file matches the original.\n";
    } else {
        std::cerr << "Test failed: Reconstructed file does not match the original.\n";
    }

    return 0;
}
