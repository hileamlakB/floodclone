
#include "ThreadPool.h"
#include "FileManager.h"
#include <iostream>
#include <string>
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <iomanip>

std::string calculate_checksum(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }

    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    char buffer[8192];  // Buffer to read the file in chunks
    while (file.read(buffer, sizeof(buffer))) {
        SHA256_Update(&sha256, buffer, file.gcount());
    }
    // Final update for any remaining bytes
    SHA256_Update(&sha256, buffer, file.gcount());

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);

    // Convert hash to a string
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

int main() {
    ThreadPool senderthreadPool(4);

    // Step 1: Create the sender FileManager
    FileManager senderFileManager("tests/test_file.txt", 0, "127.0.0.1", "tests/sender_pieces", &senderthreadPool, true, "");
    std::cout << "Sender FileManager created and file split into pieces.\n";

    // Wait for all tasks in thread pool to complete
    senderthreadPool.join();

    // Save metadata to file for the receiver
    std::string metadata_file_path = senderFileManager.save_metadata();
    std::cout << "Sender metadata saved to " << metadata_file_path << "\n";

    ThreadPool recieverthreadpool(4);


    // Step 2: Create the receiver FileManager
    FileManager receiverFileManager("", 0, "127.0.0.1", "tests/receiver_pieces", &recieverthreadpool, false, metadata_file_path);
    std::cout << "Receiver FileManager created from metadata.\n";

    // Step 3: Simulate piece transfer from sender to receiver
    for (size_t i = 0; i < senderFileManager.get_metadata().numPieces; ++i) {
        // Send the piece from sender
        std::string piece_data = senderFileManager.send(i);
        
        // Receive the piece on the receiver side
        receiverFileManager.receive(piece_data, i);
    }

    recieverthreadpool.wait();

    // Step 4: Reconstruct the file on the receiver side
    receiverFileManager.reconstruct();

   

    // Step 5: Calculate checksums for the original and reconstructed files
    std::string original_file_hash = calculate_checksum("tests/test_file.txt");
    std::string reconstructed_file_hash = calculate_checksum("tests/receiver_pieces/reconstructed_test_file.txt");

    // Step 6: Compare hashes
    if (original_file_hash == reconstructed_file_hash) {
        std::cout << "Test passed: Reconstructed file matches the original.\n";
    } else {
        std::cout << "Test failed: Reconstructed file does not match the original.\n";
    }

    return 0;
}
