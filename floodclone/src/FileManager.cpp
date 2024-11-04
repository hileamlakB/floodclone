#include "FileManager.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <filesystem>
#include <regex>
#include <openssl/sha.h>


using namespace std;
namespace fs = std::filesystem;

// Constructor implementation for FileManager
FileManager::FileManager(const string& file_path, const int ipiece_size, const string& node_ip, const string& pieces_folder, ThreadPool* thread_pool)
    : file_path(file_path), piece_size(ipiece_size == 0 ? 16384 : ipiece_size), node_ip(node_ip), num_pieces(0), pieces_folder(pieces_folder), thread_pool(thread_pool) {

    
    ifstream fileStream(file_path, ios::binary);
    if (!fileStream) {
        throw runtime_error("Cannot open file: " + file_path);
    }

    fileStream.seekg(0, ios::end);
    size_t file_size = fileStream.tellg();
    fileStream.seekg(0, ios::beg);

    
    num_pieces = static_cast<size_t>(ceil(static_cast<double>(file_size) / piece_size));
    
    verify_ip(node_ip);

    // Handle piece folder creation (mandatory check and creation if not exists)
    if (!fs::exists(pieces_folder)) {
        fs::create_directories(pieces_folder);
    } else if (!fs::is_directory(pieces_folder)) {
        throw runtime_error("Provided piece folder path is not a directory: " + pieces_folder);
    }

    // Initialize file metadata
    // md5 hash
    file_metadata.fileId = "example_file_id"; 
    file_metadata.filename = fs::path(file_path).filename().string();
    file_metadata.numPieces = num_pieces;
    file_metadata.pieces.resize(num_pieces);

    // Populate metadata for each piece
    // do this in a separate thread to not take away time
    // lock the metadat
    for (size_t i = 0; i < num_pieces; ++i) {
        thread_pool->enqueue([this, i] {
            split(i); // Each piece processing is handled by split(i) within the thread pool
        });
    }
        
}

void FileManager::split(size_t i) {
    ifstream fileStream(file_path, ios::binary);
    if (!fileStream) {
        throw runtime_error("Cannot open file: " + file_path);
    }

    // Seek to the beginning of the i-th piece
    fileStream.seekg(piece_size * i, ios::beg);
    
    // Read the i-th chunk of the file
    vector<char> buffer(piece_size);
    fileStream.read(buffer.data(), piece_size);
    size_t bytes_read = fileStream.gcount();
    std::string piece_data(buffer.data(), bytes_read);

    // Calculate checksum
    PieceMetaData pieceMeta;
    pieceMeta.srcs.push_back(array<char, IP4_LENGTH>());
    strncpy(pieceMeta.srcs.back().data(), node_ip.c_str(), IP4_LENGTH - 1);
    pieceMeta.checksum = calculate_checksum(piece_data);

    // Define the output file path for this piece
    std::filesystem::path piece_path = std::filesystem::path(pieces_folder) / ("piece_" + std::to_string(i));
    std::ofstream piece_file(piece_path, std::ios::binary);
    if (!piece_file) {
        throw runtime_error("Cannot open output file: " + piece_path.string());
    }

    // Write the piece data to the file and close it
    piece_file.write(piece_data.data(), bytes_read);
    piece_file.close();

    // Lock and update file metadata
    {
        std::lock_guard<std::mutex> lock(metadata_mutex);
        file_metadata.pieces[i] = pieceMeta;
    }
}

std::string FileManager::calculate_checksum(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// Private method to check the validity of an IP address
void FileManager::verify_ip(const string& ip) {
    
    regex ipv4_pattern("^(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})$");
    smatch match;
    if (!regex_match(ip, match, ipv4_pattern)) {
        throw runtime_error("Invalid IP address format.");
    }
    for (int i = 1; i <= 4; ++i) {
        int octet = stoi(match[i].str());
        if (octet < 0 || octet > 255) {
            throw runtime_error("Invalid IP address range.");
        }
    }
}


void FileManager::save_metadata() {
    // Serialize metadata
    std::string serialized_metadata = file_metadata.serialize();

    // Construct the metadata file path: pieces_folder/original_filename.metadata
    std::filesystem::path metadata_path = std::filesystem::path(pieces_folder) / (file_metadata.filename + ".metadata");

    // Open the file and write serialized metadata
    std::ofstream metadata_file(metadata_path, std::ios::binary);
    if (!metadata_file) {
        throw std::runtime_error("Cannot open metadata file: " + metadata_path.string());
    }

    // Write the metadata and close the file
    metadata_file.write(serialized_metadata.data(), serialized_metadata.size());
    metadata_file.close();
}


void FileManager::merge(size_t i) {

    // this code is thread unsfae if it is called  for the same i 
    // and will endup overiwting the same portion of the merged file. 
    
    // Define paths
    std::filesystem::path piece_path = std::filesystem::path(pieces_folder) / ("piece_" + std::to_string(i));
    std::filesystem::path merged_file_path = std::filesystem::path(pieces_folder) / ("reconstructed_" + file_metadata.filename);

    // Open the piece file in read-only mode
    int piece_fd = open(piece_path.c_str(), O_RDONLY);
    if (piece_fd < 0) {
        throw std::runtime_error("Cannot open piece file: " + piece_path.string());
    }

    // Open the merged file in write-only mode
    int merged_fd = open(merged_file_path.c_str(), O_WRONLY);
    if (merged_fd < 0) {
        close(piece_fd); // Clean up on error
        throw std::runtime_error("Cannot open merged file: " + merged_file_path.string());
    }

    // Calculate the offset for the i-th piece
    off_t offset = i * piece_size;

    // Get the size of the piece
    off_t piece_size = std::filesystem::file_size(piece_path);

    // Copy the file data from piece_fd to merged_fd at the correct offset
    if (sendfile(merged_fd, piece_fd, &offset, piece_size) == -1) {
        close(piece_fd);
        close(merged_fd);
        throw std::runtime_error("Error copying data with sendfile");
    }

    // Close file descriptors
    close(piece_fd);
    close(merged_fd);
}

void FileManager::reconstruct() {
    // Loop through all pieces and enqueue merge tasks in the thread pool
    for (size_t i = 0; i < file_metadata.numPieces; ++i) {
        // Enqueue a task to merge each piece; each task will call merge(i) for a unique i
        thread_pool->enqueue([this, i] {
            merge(i);
        });
    }
}

