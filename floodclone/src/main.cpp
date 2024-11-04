#include "ThreadPool.h"
#include "FileManager.h"
#include <iostream>

int main() {
    ThreadPool threadPool(4);

    // Step 1: Create and test the sender FileManager
    FileManager senderFileManager("tests/test_file.txt", 0, "127.0.0.1", "tests/sender_pieces", &threadPool, true, "");
    std::cout << "Sender FileManager created and file split into pieces.\n";

    // Wait for all tasks in thread pool to complete
    threadPool.join();

    // Save metadata to file for testing the receiver and capture the path
    std::string metadata_file_path = senderFileManager.save_metadata();
    std::cout << "Sender metadata saved to " << metadata_file_path << "\n";

    // Step 2: Create and test the receiver FileManager
    FileManager receiverFileManager("", 0, "127.0.0.1", "tests/receiver_pieces", &threadPool, false, metadata_file_path);
    std::cout << "Receiver FileManager created from metadata.\n";

    // Verify that metadata was correctly deserialized
    std::cout << "Receiver metadata file ID: " << receiverFileManager.get_metadata().fileId << "\n";
    std::cout << "Receiver metadata filename: " << receiverFileManager.get_metadata().filename << "\n";
    std::cout << "Receiver number of pieces: " << receiverFileManager.get_metadata().numPieces << "\n";

    return 0;
}
