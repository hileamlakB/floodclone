#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <cstring>
#include <map>
#include <optional>
#include "FileManager.h"

typedef enum : uint16_t {
    META_REQ = 1,
    META_RES = 2,
    PIECE_REQ = 3,
    PIECE_RES = 4,
    SINGLE_PIECE = 1 << 0,  // 0001
    PIECE_RANGE  = 1 << 1,  // 0010
    PIECE_LIST   = 1 << 2   // 0100
} RequestType;

struct RequestHeader {
    RequestType type;
    uint32_t payloadSize;

    std::vector<char> serialize() const {
        std::vector<char> data(sizeof(RequestHeader));  // Use full struct size
        assert(data.size() == sizeof(RequestHeader) && "Serialized size must match struct size");
        
        // Copy the entire struct including padding
        std::memcpy(data.data(), this, sizeof(RequestHeader));
        return data;
    }

    static RequestHeader deserialize(const std::vector<char>& data) {
        assert(data.size() == sizeof(RequestHeader) && "Data size must match struct size");
        
        RequestHeader header;
        std::memcpy(&header, data.data(), sizeof(RequestHeader));
        return header;
    }
};

struct PieceRequest {
    uint32_t types;  // Bitfield of RequestType
    // For single piece
    size_t pieceIndex;
    // For range
    std::vector<std::pair<size_t, size_t>> ranges;  // (start, end) pairs
    // For specific pieces
    std::vector<size_t> pieces;

    std::vector<char> serialize() const {
        std::vector<char> data;
        data.reserve(1024);  // Reserve some reasonable space

        // Write request type flags
        data.insert(data.end(), reinterpret_cast<const char*>(&types),
                   reinterpret_cast<const char*>(&types) + sizeof(types));

        if (types & SINGLE_PIECE) {
            data.insert(data.end(), reinterpret_cast<const char*>(&pieceIndex),
                       reinterpret_cast<const char*>(&pieceIndex) + sizeof(pieceIndex));
        }

        if (types & PIECE_RANGE) {
            size_t range_size = ranges.size();
            data.insert(data.end(), reinterpret_cast<const char*>(&range_size),
                       reinterpret_cast<const char*>(&range_size) + sizeof(range_size));
            for (const auto& range : ranges) {
                data.insert(data.end(), reinterpret_cast<const char*>(&range.first),
                           reinterpret_cast<const char*>(&range.first) + sizeof(range.first));
                data.insert(data.end(), reinterpret_cast<const char*>(&range.second),
                           reinterpret_cast<const char*>(&range.second) + sizeof(range.second));
            }
        }

        if (types & PIECE_LIST) {
            size_t list_size = pieces.size();
            data.insert(data.end(), reinterpret_cast<const char*>(&list_size),
                       reinterpret_cast<const char*>(&list_size) + sizeof(list_size));
            for (size_t piece : pieces) {
                data.insert(data.end(), reinterpret_cast<const char*>(&piece),
                           reinterpret_cast<const char*>(&piece) + sizeof(piece));
            }
        }

        return data;
    }

    static PieceRequest deserialize(const std::vector<char>& data) {
        PieceRequest req;
        size_t offset = 0;

        std::memcpy(&req.types, data.data() + offset, sizeof(req.types));
        offset += sizeof(req.types);

        if (req.types & SINGLE_PIECE) {
            std::memcpy(&req.pieceIndex, data.data() + offset, sizeof(size_t));
            offset += sizeof(size_t);
        }

        if (req.types & PIECE_RANGE) {
            size_t range_size;
            std::memcpy(&range_size, data.data() + offset, sizeof(size_t));
            offset += sizeof(size_t);
            
            for (size_t i = 0; i < range_size; i++) {
                size_t start, end;
                std::memcpy(&start, data.data() + offset, sizeof(size_t));
                offset += sizeof(size_t);
                std::memcpy(&end, data.data() + offset, sizeof(size_t));
                offset += sizeof(size_t);
                req.ranges.emplace_back(start, end);
            }
        }

        if (req.types & PIECE_LIST) {
            size_t list_size;
            std::memcpy(&list_size, data.data() + offset, sizeof(size_t));
            offset += sizeof(size_t);
            
            for (size_t i = 0; i < list_size; i++) {
                size_t piece;
                std::memcpy(&piece, data.data() + offset, sizeof(size_t));
                offset += sizeof(size_t);
                req.pieces.push_back(piece);
            }
        }

        return req;
    }
};

struct FileMetaData;
class ThreadPool;

class ConnectionManager {
public:
    // Constructor for client mode (no FileManager needed)
    ConnectionManager(const std::string& localAddress, int localPort, ThreadPool& threadPool)
        : localAddress_(localAddress), localPort_(localPort), threadPool_(threadPool), fileManager_(nullptr) {}
    
    // Constructor for server mode (requires FileManager)
    ConnectionManager(const std::string& localAddress, int localPort, ThreadPool& threadPool, FileManager& fileManager)
        : localAddress_(localAddress), localPort_(localPort), threadPool_(threadPool), fileManager_(&fileManager) {}
    
    ~ConnectionManager();

    // Server operations
    void start_listening();
    void stop_listening();

    // Client operations
    FileMetaData request_metadata(const std::string& destAddress, int destPort);
    void request_pieces(const std::string& destAddress, int destPort, 
                                     size_t single_piece,
                                     const std::vector<std::pair<size_t, size_t>>& ranges,
                                     const std::vector<size_t>& piece_list);
    void close_connection(const std::string& destAddress, int destPort);

    void set_file_manager(FileManager& manager) {
        fileManager_ = &manager;
    }

private:
    std::string localAddress_;
    int localPort_;
    int wake_fd_ = -1;  
    ThreadPool& threadPool_;
    FileManager* fileManager_;  // Optional pointer to FileManager

    int listeningSocket_;
    std::atomic<bool> isListening_;
    std::mutex connectionMapMutex_;
    std::mutex listeningMutex_;
    std::mutex  fdLocksMapMutex_;
    std::map<std::pair<std::string, int>, int> connectionMap_;
    std::unordered_map<int, std::unique_ptr<std::mutex>> fdLocks_;  // fd -> lock

    // Helper to get or create lock for a fd
    std::mutex& fd_lock(int fd) {
        std::lock_guard<std::mutex> lock(fdLocksMapMutex_);
        auto it = fdLocks_.find(fd);
        if (it == fdLocks_.end()) {
            auto [inserted_it, success] = fdLocks_.emplace(fd, std::make_unique<std::mutex>());
            return *inserted_it->second;
        }
        return *it->second;
    }

    // Helper to remove lock when fd is closed
    void dfd_lock(int fd) {
        std::lock_guard<std::mutex> lock(fdLocksMapMutex_);
        fdLocks_.erase(fd);
    }

    // Helper methods
    void process_request(int fd);
    void process_meta_request(int fd, const RequestHeader& header);
    void process_piece_request(int fd, const RequestHeader& header);
    int connect_to(const std::string& destAddress, int destPort);
    void send_all(int fd, const std::string_view& data);
    void receive_all(int found, char* buffer, size_t size);
};

#endif // CONNECTION_MANAGER_H