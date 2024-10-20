#include "FileManager.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <filesystem>
#include <sstream>
#include <openssl/sha.h>
#include <arpa/inet.h>


FileManager::FileManager(const std::string& filePath, int pieceSize, const std::string& srcIp, const std::string& piecesPath)
    : filePath(filePath), pieceSize(pieceSize > 0 ? pieceSize : 16384), srcIp(srcIp), piecesPath(piecesPath) {
    verifyIp(srcIp);
    
    assert(!filePath.empty());
    assert(!piecesPath.empty());

    // assert file path exists
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("File does not exist: " + filePath);
    }

    // assert piecesPath exists
    if (!std::filesystem::exists(piecesPath)) {
        throw std::runtime_error("Pieces path does not exist: " + piecesPath);
    }

    fileId = calculateFileId(filePath);
    filename = std::filesystem::path(filePath).filename().string();
    fileDir = piecesPath + "/dir_" + fileId;
    std::filesystem::create_directories(fileDir);
    fileSize = std::filesystem::file_size(filePath);
    numPieces = static_cast<int>(std::ceil(static_cast<double>(fileSize) / pieceSize));
}

void FileManager::updateMetaData(Metadata& new_metadata){
    metadata = new_metadata;
}

void FileManager::verifyIp(const std::string& srcIp) const {
    sockaddr_in sa;
    if (inet_pton(AF_INET, srcIp.c_str(), &(sa.sin_addr)) <= 0) {
        throw std::invalid_argument("Invalid IP address format: " + srcIp);
    }
}

std::string FileManager::calculateFileId(const std::string& filePath) const {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(filePath.c_str()), filePath.size(), hash);
    std::stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        ss << std::hex << static_cast<int>(hash[i]);
    }
    return ss.str();
}

void FileManager::splitPieces() {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open file for reading: " + filePath);
    }

    for (int i = 0; i < numPieces; ++i) {
        std::vector<char> buffer(pieceSize);
        file.read(buffer.data(), pieceSize);
        size_t bytesRead = file.gcount();
        buffer.resize(bytesRead);

        std::ofstream pieceFile(fileDir + "/piece_" + std::to_string(i), std::ios::binary);
        pieceFile.write(buffer.data(), bytesRead);
        availablePieces.insert(i);
    }
}

void FileManager::receivePiece(int pieceId, const std::vector<char>& data, const std::string& srcIp) {
    verifyIp(srcIp);
    assert(missingPieces.find(pieceId) != missingPieces.end());

    std::ofstream pieceFile(fileDir + "/piece_" + std::to_string(pieceId), std::ios::binary);
    pieceFile.write(data.data(), data.size());

    availablePieces.insert(pieceId);
    missingPieces.erase(pieceId);
}

std::vector<char> FileManager::sendPiece(int pieceId) const {
    assert(availablePieces.find(pieceId) != availablePieces.end());

    std::ifstream pieceFile(fileDir + "/piece_" + std::to_string(pieceId), std::ios::binary);
    std::vector<char> buffer((std::istreambuf_iterator<char>(pieceFile)), std::istreambuf_iterator<char>());
    return buffer;
}

void FileManager::reassemble() const {
    std::ofstream outputFile("reconstructed_" + filename, std::ios::binary);
    if (!outputFile) {
        throw std::runtime_error("Unable to create output file for reassembly");
    }

    for (int i = 0; i < numPieces; ++i) {
        std::ifstream pieceFile(fileDir + "/piece_" + std::to_string(i), std::ios::binary);
        if (!pieceFile) {
            throw std::runtime_error("Missing piece: " + std::to_string(i));
        }
        outputFile << pieceFile.rdbuf();
    }
}

std::set<int> FileManager::getMissingPieces() const {
    return missingPieces;
}

std::set<int> FileManager::getAvailablePieces() const {
    return availablePieces;
}


FileManager FileManagerFromMetadata(const std::string& metadataPath, const std::string& srcIp, const std::string& piecesPath) {
    // Create a Metadata instance and load data from the metadata file
    Metadata metadata;
    metadata.deserialize(metadataPath);

    // Use the metadata to create a FileManager instance
    FileManager fileManager(metadata.filename, 16384, srcIp, piecesPath);
    
    // Load metadata to initialize FileManager's internal state
    fileManager.updateMetaData(metadata);
    
    return fileManager;
}

