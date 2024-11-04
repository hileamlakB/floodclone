#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <vector>
#include <string>
#include <sstream>
#include <array>
#include <mutex>
#include "ThreadPool.h"

using namespace std;

constexpr size_t IP4_LENGTH = 15; // constant length for IPv4 address strings
class ThreadPool;

// contains information about a single piece of file
struct PieceMetaData{
    vector<array<char, IP4_LENGTH>> srcs; // list of ip addresses that are known to have the piece, fixed length for IPv4
    std::string checksum; 


    // serialize: converts the piece metadata to a binary string
    string serialize() const {
        stringstream ss;
        
        // Serialize the source IP addresses
        size_t src_count = srcs.size();
        ss.write(reinterpret_cast<const char*>(&src_count), sizeof(src_count));
        for(const auto& src : srcs) {
            ss.write(src.data(), IP4_LENGTH);
        }

        // Serialize the checksum
        size_t checksum_len = checksum.length();
        ss.write(reinterpret_cast<const char*>(&checksum_len), sizeof(checksum_len));
        ss.write(checksum.data(), checksum_len);

        return ss.str();
    }

    // Deserialize: populates the metadata from a binary string
    static PieceMetaData deserialize(const string& binary) {
        PieceMetaData pieceMeta;
        stringstream ss(binary);

        // Deserialize the source IP addresses
        size_t src_count;
        ss.read(reinterpret_cast<char*>(&src_count), sizeof(src_count));
        pieceMeta.srcs.resize(src_count);
        for(size_t i = 0; i < src_count; ++i) {
            ss.read(pieceMeta.srcs[i].data(), IP4_LENGTH);
        }

        // Deserialize the checksum
        size_t checksum_len;
        ss.read(reinterpret_cast<char*>(&checksum_len), sizeof(checksum_len));
        pieceMeta.checksum.resize(checksum_len);
        ss.read(&pieceMeta.checksum[0], checksum_len);

        return pieceMeta;
    }
};

// contains information about a file
struct FileMetaData {
    string fileId;         // unique hash of a specific file
    string filename;       // string name of the file
    size_t numPieces;         // number of pieces that make up the file
    vector<PieceMetaData> pieces; // information about each of the pieces that make up a file 

    // serialize: converts the file metadata to a binary string
    string serialize() const {
        stringstream ss;
        size_t fileId_len = fileId.length();
        ss.write(reinterpret_cast<const char*>(&fileId_len), sizeof(fileId_len));
        ss.write(fileId.data(), fileId_len);

        size_t filename_len = filename.length();
        ss.write(reinterpret_cast<const char*>(&filename_len), sizeof(filename_len));
        ss.write(filename.data(), filename_len);

        ss.write(reinterpret_cast<const char*>(&numPieces), sizeof(numPieces));

        for(const auto& piece : pieces) {
            string serialized_piece = piece.serialize();
            size_t piece_len = serialized_piece.length();
            ss.write(reinterpret_cast<const char*>(&piece_len), sizeof(piece_len));
            ss.write(serialized_piece.data(), piece_len);
        }
        return ss.str();
    }

    // deserialize: populates the metadata from a binary string
    static FileMetaData deserialize(const string& binary) {
        FileMetaData fileMeta;
        stringstream ss(binary);

        size_t fileId_len;
        ss.read(reinterpret_cast<char*>(&fileId_len), sizeof(fileId_len));
        fileMeta.fileId.resize(fileId_len);
        ss.read(&fileMeta.fileId[0], fileId_len);

        size_t filename_len;
        ss.read(reinterpret_cast<char*>(&filename_len), sizeof(filename_len));
        fileMeta.filename.resize(filename_len);
        ss.read(&fileMeta.filename[0], filename_len);

        ss.read(reinterpret_cast<char*>(&fileMeta.numPieces), sizeof(fileMeta.numPieces));

        fileMeta.pieces.resize(fileMeta.numPieces);
        for(size_t i = 0; i < fileMeta.numPieces; ++i) {
            size_t piece_len;
            ss.read(reinterpret_cast<char*>(&piece_len), sizeof(piece_len));
            string piece_binary(piece_len, '\0');
            ss.read(&piece_binary[0], piece_len);
            fileMeta.pieces[i] = PieceMetaData::deserialize(piece_binary);
        }
        return fileMeta;
    }
};

class FileManager {
public:
    FileManager(const std::string& file_path, size_t ipiece_size, const std::string& node_ip,
                const std::string& pieces_folder, ThreadPool* thread_pool, bool is_source, const std::string& metadata_file_path);



    const FileMetaData& get_metadata() const;
    std::string  save_metadata();
    void reconstruct();  // reconutructs a file based on pieces
    void deconstruct(); 
    void receive(const std::string& binary_data, size_t i);
    std::string send(size_t i);

private:
   
    string file_path;                       // path to original path
    size_t piece_size;                              
    FileMetaData file_metadata; 
    std::mutex metadata_mutex; 
    
    string node_ip;                 
    size_t num_pieces;
    string pieces_folder;

    ThreadPool *thread_pool;
    
     bool is_source; 
    

    void verify_ip(const string& ip);
    std::string calculate_checksum(const std::string& data); 
    void split(size_t i);  // splits the i-th peice file into piece_i 
    void merge(size_t i); // Merges the i-th piece into the main file
    void initialize_source();
    void initialize_receiver(const std::string& metadata_file_path);
};

FileManager FileManagerFromMetadata(const string& metadataPath, const string& srcIp, const string& piecePath);

#endif // FILEMANAGER_H
