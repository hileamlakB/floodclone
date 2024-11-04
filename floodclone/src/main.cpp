#include "FileManager.h"
#include <iostream>


int main() {
    
    ThreadPool threadPool(4);
    FileManager senderFileManager("tests/test_file.txt", 0, "127.0.0.1", "tests/sender_pieces", &threadPool);

    std::cout<<"MADE IT TO HERE";
    
    // Wait for all tasks to complete
    threadPool.join();

    std::cout<<"MADE IT TO HERE 2";

    // Optionally, save metadata after all threads are done
    senderFileManager.save_metadata();

    return 0;
}
