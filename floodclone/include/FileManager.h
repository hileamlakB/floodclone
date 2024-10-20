#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <fstream>
#include <filesystem>
#include <cassert>
#include <openssl/sha.h>
#include <arpa/inet.h>
#include <cmath>
#include <iostream>
#include <sstream>

struct Metadata {
    std::string fileId;
    std::string filename;
    std::string srcIp;
    int numPieces;
    std::vector<std::map<std::string, std::vector<std::string>>> pieces; // Metadata for each piece

    // Serialize metadata to a binary file
    void serialize(const std::string& path) const {
        std::ofstream outFile(path, std::ios::binary);
        if (!outFile) {
            throw std::runtime_error("Unable to open file for writing metadata: " + path);
        }
        size_t fileIdSize = fileId.size();
        size_t filenameSize = filename.size();
        size_t srcIpSize = srcIp.size();

        outFile.write(reinterpret_cast<const char*>(&fileIdSize), sizeof(fileIdSize));
        outFile.write(fileId.data(), fileIdSize);
        outFile.write(reinterpret_cast<const char*>(&filenameSize), sizeof(filenameSize));
        outFile.write(filename.data(), filenameSize);
        outFile.write(reinterpret_cast<const char*>(&srcIpSize), sizeof(srcIpSize));
        outFile.write(srcIp.data(), srcIpSize);
        outFile.write(reinterpret_cast<const char*>(&numPieces), sizeof(numPieces));

        // Write piece metadata
        size_t piecesCount = pieces.size();
        outFile.write(reinterpret_cast<const char*>(&piecesCount), sizeof(piecesCount));
        for (const auto& piece : pieces) {
            size_t trackersCount = piece.at("tracker").size();
            outFile.write(reinterpret_cast<const char*>(&trackersCount), sizeof(trackersCount));
            for (const auto& tracker : piece.at("tracker")) {
                size_t trackerSize = tracker.size();
                outFile.write(reinterpret_cast<const char*>(&trackerSize), sizeof(trackerSize));
                outFile.write(tracker.data(), trackerSize);
            }
        }
    }

    // Deserialize metadata from a binary file
    void deserialize(const std::string& path) {
        std::ifstream inFile(path, std::ios::binary);
        if (!inFile) {
            throw std::runtime_error("Unable to open file for reading metadata: " + path);
        }
        size_t fileIdSize, filenameSize, srcIpSize;

        inFile.read(reinterpret_cast<char*>(&fileIdSize), sizeof(fileIdSize));
        fileId.resize(fileIdSize);
        inFile.read(&fileId[0], fileIdSize);

        inFile.read(reinterpret_cast<char*>(&filenameSize), sizeof(filenameSize));
        filename.resize(filenameSize);
        inFile.read(&filename[0], filenameSize);

        inFile.read(reinterpret_cast<char*>(&srcIpSize), sizeof(srcIpSize));
        srcIp.resize(srcIpSize);
        inFile.read(&srcIp[0], srcIpSize);

        inFile.read(reinterpret_cast<char*>(&numPieces), sizeof(numPieces));

        // Read piece metadata
        size_t piecesCount;
        inFile.read(reinterpret_cast<char*>(&piecesCount), sizeof(piecesCount));
        pieces.resize(piecesCount);
        for (auto& piece : pieces) {
            size_t trackersCount;
            inFile.read(reinterpret_cast<char*>(&trackersCount), sizeof(trackersCount));
            std::vector<std::string> trackers(trackersCount);
            for (auto& tracker : trackers) {
                size_t trackerSize;
                inFile.read(reinterpret_cast<char*>(&trackerSize), sizeof(trackerSize));
                tracker.resize(trackerSize);
                inFile.read(&tracker[0], trackerSize);
            }
            piece["tracker"] = trackers;
        }
    }
};

class FileManager {
public:
    // Constructor to initialize FileManager with file path, piece size, source IP, and pieces path
    FileManager(const std::string& filePath, int pieceSize, const std::string& srcIp, const std::string& piecesPath);

    // Split the file into pieces of predefined size and store them on disk
    void splitPieces();

    // Receive a piece from another node and store it
    void receivePiece(int pieceId, const std::vector<char>& data, const std::string& srcIp);

    // Send a piece as a bytes vector
    std::vector<char> sendPiece(int pieceId) const;

    // Reassemble the complete file from pieces
    void reassemble() const;

    // Get the set of missing pieces
    std::set<int> getMissingPieces() const;

    // Get the set of available pieces
    std::set<int> getAvailablePieces() const;


    // Save metadata to a file
    void saveMetadata(const std::string& metadataPath) const;

    void updateMetaData(Metadata& metadata);

private:
    // Verify if the provided IP address is in a valid format
    void verifyIp(const std::string& srcIp) const;

    // Calculate a unique ID for the file using SHA-1 hash
    std::string calculateFileId(const std::string& filePath) const;

    // Member variables
    std::string filePath;
    int pieceSize;
    std::string srcIp;
    std::string fileId;
    std::string filename;
    std::string fileDir;
    std::string piecesPath;
    size_t fileSize;
    int numPieces;
    std::set<int> availablePieces;
    std::set<int> missingPieces;
    Metadata metadata;
};

FileManager FileManagerFromMetadata(const std::string& metadataPath, const std::string& srcIp, const std::string& piecePath);

#endif // FILEMANAGER_H
