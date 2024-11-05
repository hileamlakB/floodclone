#include "FileManager.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <filesystem>
#include <regex>
#include <openssl/sha.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <iostream>


using namespace std;
namespace fs = std::filesystem;



FileManager::FileManager(const std::string& file_path, size_t ipiece_size, const std::string& node_ip,
                         const std::string& pieces_folder, ThreadPool* thread_pool, bool is_source,
                         const std::string& metadata_file_path)
    : file_path(file_path), piece_size(ipiece_size == 0 ? 16384 : ipiece_size), node_ip(node_ip),
      num_pieces(0), pieces_folder(pieces_folder), thread_pool(thread_pool), is_source(is_source) 
{

    verify_ip(node_ip);

    // Handle piece folder creation
    if (!fs::exists(pieces_folder)) {
        fs::create_directories(pieces_folder);
    } else if (!fs::is_directory(pieces_folder)) {
        throw runtime_error("Provided piece folder path is not a directory: " + pieces_folder);
    }

    // Initialize as source or receiver based on is_source flag
    if (is_source) {
        initialize_source();  // Source mode, calculate and populate metadata from file
    } else {
        initialize_receiver(metadata_file_path);  // Receiver mode, initialize from metadata file
    }
}

// Function to initialize source-specific logic
void FileManager::initialize_source() {
    // Open the file and calculate file size
    ifstream fileStream(file_path, ios::binary);
    if (!fileStream) {
        throw runtime_error("Cannot open file: " + file_path);
    }

    fileStream.seekg(0, ios::end);
    size_t file_size = fileStream.tellg();
    fileStream.seekg(0, ios::beg);

    // Calculate number of pieces
    num_pieces = (file_size + piece_size - 1) / piece_size;

    // Initialize metadata with actual data for source
    file_metadata.fileId = "example_file_id";  // Replace with actual hash function
    file_metadata.filename = fs::path(file_path).filename().string();
    file_metadata.fileSize = file_size;
    file_metadata.numPieces = num_pieces;
    file_metadata.pieces.resize(num_pieces);

    // Populate metadata and split in parallel using thread pool
    for (size_t i = 0; i < num_pieces; ++i) {
        thread_pool->enqueue([this, i] {
            split(i);  // Split each piece within the thread pool
        });
    }
}

 void  FileManager::deconstruct(){

    assert(is_source);

    // Populate metadata for each piece
    // do this in a separate thread to not take away time
    // lock the metadat
    for (size_t i = 0; i < num_pieces; ++i) {
        thread_pool->enqueue([this, i] {
            split(i); // Each piece processing is handled by split(i) within the thread pool
        });
    }

 }

void FileManager::initialize_receiver(const std::string& metadata_file_path) {
    std::ifstream metaFile(metadata_file_path, std::ios::binary);
    if (!metaFile) {
        throw std::runtime_error("Cannot open metadata file: " + metadata_file_path);
    }

    // Read the entire file content into a string
    std::string binary((std::istreambuf_iterator<char>(metaFile)), std::istreambuf_iterator<char>());
    metaFile.close();

    // Deserialize the binary string to populate file_metadata
    file_metadata = FileMetaData::deserialize(binary);

    // Set num_pieces based on deserialized metadata
    num_pieces = file_metadata.numPieces;

    std::filesystem::path reconstructed_file_path = std::filesystem::path(pieces_folder) / ("reconstructed_" + file_metadata.filename);
    merged_fd = open(reconstructed_file_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (merged_fd < 0) {
        throw std::runtime_error("Cannot create or open reconstructed file: " + reconstructed_file_path.string());
    }

    off_t total_size = num_pieces * piece_size;
    if (ftruncate(merged_fd, total_size) == -1) {
        close(merged_fd);
        throw std::runtime_error("Error setting file size for reconstructed file");
    }

    reconstructed_file = mmap(nullptr, total_size, PROT_WRITE, MAP_SHARED, merged_fd, 0);
    if (reconstructed_file == MAP_FAILED) {
        close(merged_fd);
        throw std::runtime_error("Error mapping reconstructed file into memory");
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


std::string FileManager::save_metadata() {
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

    // Return the path of the metadata file
    return metadata_path.string();
}

const FileMetaData& FileManager::get_metadata() const {
    return file_metadata;
}

void FileManager::reconstruct() {
    
    // Sync memory to file and resize to original size
    if (msync(reconstructed_file, num_pieces * piece_size, MS_SYNC) == -1) {
        munmap(reconstructed_file, num_pieces * piece_size);
        close(merged_fd);
        throw std::runtime_error("Error syncing mapped memory to file");
    }

    if (ftruncate(merged_fd, file_metadata.fileSize) == -1) {
        munmap(reconstructed_file, num_pieces * piece_size);
        close(merged_fd);
        throw std::runtime_error("Error resizing reconstructed file");
    }

    // Unmap and close
    munmap(reconstructed_file, num_pieces * piece_size);
    close(merged_fd);
    merged_fd = -1;
}




std::string FileManager::send(size_t i) {

    // this could potentially be improved using  sendfile
    // if the receiving location is also passed in as an argument
    
    std::filesystem::path piece_path = std::filesystem::path(pieces_folder) / ("piece_" + std::to_string(i));

    

    // Open the piece file in binary read mode
    std::ifstream piece_file(piece_path, std::ios::binary);
    if (!piece_file) {
        throw std::runtime_error("Cannot open piece file: " + piece_path.string());
    }

    // Move to the end once to get the file size
    size_t piece_size = std::filesystem::file_size(piece_path);

    // Allocate a string of the right size and read the file contents into it
    std::string binary_data(piece_size, '\0');

    // Return to the beginning of the file and read the contents
    piece_file.seekg(0, std::ios::beg); // Optional, since most compilers reset automatically
    piece_file.read(&binary_data[0], piece_size);

    // Close the file and return the binary data
    piece_file.close();
    return binary_data;
}


void FileManager::receive(const std::string& binary_data, size_t i) {
    off_t offset = i * piece_size;

    // copy piece to mmaped file
    thread_pool->enqueue([this, binary_data, offset] {
        std::copy(binary_data.begin(), binary_data.end(), static_cast<char*>(reconstructed_file) + offset);
    });

    // save piece to a separate file 
    thread_pool->enqueue([this, binary_data, i] {
        std::filesystem::path piece_path = std::filesystem::path(pieces_folder) / ("piece_" + std::to_string(i));

        // Write binary_data to an individual piece file
        std::ofstream piece_file(piece_path, std::ios::binary | std::ios::trunc);
        if (!piece_file) {
            throw std::runtime_error("Cannot create piece file: " + piece_path.string());
        }
        piece_file.write(binary_data.data(), binary_data.size());
    });
}



// will it be important to check the checksum, if lets stay I start using udp protocol, but if I am using tcp that won't be required rifht
// how do we know which nodes are connected to this node 

