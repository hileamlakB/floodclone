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
    PIECE_RES = 4
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
    size_t pieceIndex;

    std::vector<char> serialize() const {
        std::vector<char> data(sizeof(pieceIndex));
        std::memcpy(data.data(), &pieceIndex, sizeof(pieceIndex));
        return data;
    }

    static PieceRequest deserialize(const std::vector<char>& data) {
        PieceRequest req;
        std::memcpy(&req.pieceIndex, data.data(), sizeof(pieceIndex));
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
    std::string request_piece(const std::string& destAddress, int destPort, size_t pieceIndex);
    void close_connection(const std::string& destAddress, int destPort);

private:
    std::string localAddress_;
    int localPort_;
    ThreadPool& threadPool_;
    FileManager* fileManager_;  // Optional pointer to FileManager

    int listeningSocket_;
    bool isListening_;
    std::mutex connectionMapMutex_;
    std::mutex listeningMutex_;
    std::map<std::pair<std::string, int>, int> connectionMap_;

    // Helper methods
    void process_request(int clientSocket);
    void process_meta_request(int clientSocket, const RequestHeader& header);
    void process_piece_request(int clientSocket, const RequestHeader& header);
    void send_all(int socket, const std::string& data);
    void receive_all(int socket, char* buffer, size_t size);
    int connect_to(const std::string& destAddress, int destPort);
};

#endif // CONNECTION_MANAGER_H