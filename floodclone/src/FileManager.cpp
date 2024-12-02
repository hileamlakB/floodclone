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

#define PIECE_SIZE 16384

FileManager::FileManager(const std::string& file_path, size_t ipiece_size, const std::string& node_ip,
                         const std::string& pieces_folder, ThreadPool* thread_pool, bool is_source,
                         const FileMetaData* metadata)
    : file_path(file_path), piece_size(ipiece_size == 0 ? PIECE_SIZE : ipiece_size), node_ip(node_ip),
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
         if (!metadata) {
            throw std::runtime_error("Metadata required for receiver nodes");
        }
        initialize_receiver(*metadata);// Receiver mode, initialize from metadata file
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

    int file_fd = open(file_path.c_str(), O_RDONLY);
    if (file_fd == -1) {
        throw runtime_error("Cannot open file: " + file_path);
    }

    // Map the padded file
    mapped_file = mmap(nullptr, file_size, PROT_READ, MAP_SHARED, file_fd, 0);
    if (mapped_file == MAP_FAILED) {
        close(file_fd);
        throw runtime_error("Failed to mmap source file");
    }

    


    // Initialize metadata with actual data for source
    file_metadata.fileId = "file_hash";  // Replace with actual hash function if I endup implementing relaibility
    file_metadata.filename = fs::path(file_path).filename().string();
    file_metadata.fileSize = file_size;
    file_metadata.numPieces = num_pieces;
    file_metadata.pieces.resize(num_pieces);

    // Initialize piece status and metadata
    for (size_t i = 0; i < num_pieces; ++i) {
        piece_status.emplace_back(true);  // All pieces are immediately available
        
        // Set up piece metadata
        PieceMetaData pieceMeta;
        pieceMeta.srcs.push_back(array<char, IP4_LENGTH>());
        strncpy(pieceMeta.srcs.back().data(), node_ip.c_str(), IP4_LENGTH - 1);
        // No checksum needed since using TCP
        
        // Store metadata
        file_metadata.pieces[i] = pieceMeta;
    }
    // deconstruct();
}

 void  FileManager::deconstruct(){

    assert(is_source);

    // Populate metadata for each piece
    // do this in a separate thread to not take away time
    // lock the metadat
    for (size_t i = 0; i < num_pieces; ++i) {
        piece_status.emplace_back(false);
        thread_pool->enqueue([this, i] {
            split(i); 
            piece_status[i].store(true);
        });
    }
 }


void FileManager::initialize_receiver(const FileMetaData& metadata) {
    // Directly set the metadata without file reading/deserialization
    file_metadata = metadata;
    num_pieces = metadata.numPieces;

    // std::filesystem::path mmaped_fil_path = std::filesystem::path(pieces_folder) / ("reconstructed_" + file_metadata.filename);
    // std::filesystem::path mmaped_fil_path = std::filesystem::path(pieces_folder) / (file_metadata.filename);
    merged_fd = open(file_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (merged_fd < 0) {
        throw std::runtime_error("Cannot create or open reconstructed file: " + file_path);
    }

    off_t total_size = num_pieces * piece_size;
    if (ftruncate(merged_fd, total_size) == -1) {
        close(merged_fd);
        throw std::runtime_error("Error setting file size for reconstructed file");
    }

    mapped_file = mmap(nullptr, total_size, PROT_WRITE, MAP_SHARED, merged_fd, 0);
    if (mapped_file == MAP_FAILED) {
        close(merged_fd);
        throw std::runtime_error("Error mapping reconstructed file into memory");
    }

    for (size_t i = 0; i < num_pieces; ++i) {
        piece_status.emplace_back(false);
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
    // pieceMeta.checksum = calculate_checksum(piece_data); // not required since we ae using tcp and no loss happens

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
    if (msync(mapped_file, num_pieces * piece_size, MS_SYNC) == -1) {
        munmap(mapped_file, num_pieces * piece_size);
        close(merged_fd);
        throw std::runtime_error("Error syncing mapped memory to file");
    }

    if (ftruncate(merged_fd, file_metadata.fileSize) == -1) {
        munmap(mapped_file, num_pieces * piece_size);
        close(merged_fd);
        throw std::runtime_error("Error resizing reconstructed file");
    }

    // Unmap and close
    munmap(mapped_file, num_pieces * piece_size);
    close(merged_fd);
    merged_fd = -1;
}





std::string_view FileManager::send(size_t i) {
    assert(i < num_pieces);
    assert(piece_status[i].load());
    
    size_t offset = i * piece_size;
    const char* piece_data = static_cast<const char*>(mapped_file) + offset;
    
    if (i == num_pieces - 1) {
        // Last piece - might be smaller
        size_t last_piece_size = file_metadata.fileSize - offset;
        if (last_piece_size < piece_size) {
            // Need to pad the last piece
            static std::string padded_piece;  // Static to avoid reallocation
            padded_piece.resize(piece_size);
            std::memcpy(padded_piece.data(), piece_data, last_piece_size);
            std::memset(padded_piece.data() + last_piece_size, 0, piece_size - last_piece_size);
            return std::string_view(padded_piece);
        }
    }
    
    return std::string_view(piece_data, piece_size);
}


void FileManager::receive(const std::string_view& binary_data, size_t i) {
    assert(i < num_pieces);
    assert(binary_data.size() <= piece_size);  // Safety check

    if (piece_status[i].load()) {
        std::cout << "RECEIVING a piece that already exists" << std::endl;
        return;
    }

    off_t offset = i * piece_size;

    // Use memcpy directly and avoid string copy in lambda capture
    thread_pool->enqueue([this, data_ptr = binary_data.data(), 
                         data_size = binary_data.size(), offset, i] {
        std::memcpy(static_cast<char*>(mapped_file) + offset, data_ptr, data_size);
        piece_status[i].store(true);
    });
}


bool FileManager::has_piece(size_t i)
{
    assert(i < num_pieces);
    return piece_status[i].load();
}


char* FileManager::get_piece_buffer(size_t i, size_t& size) {
        assert(i < num_pieces);
        if (piece_status[i].load()) {
            return nullptr;  // Already have this piece
        }
        size = piece_size;
        return static_cast<char*>(mapped_file) + (i * piece_size);
}

