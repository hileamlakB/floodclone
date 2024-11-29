#ifndef FLOODCLONE_H
#define FLOODCLONE_H

#include <string>
#include <nlohmann/json.hpp>
#include <memory>
#include "ThreadPool.h"
#include "FileManager.h"
#include "ConnectionManager.h"

struct Arguments {
    std::string mode;
    std::string node_name;
    std::string src_name;
    std::string file_path;
    std::string pieces_dir;
    std::string timestamp_file;
    nlohmann::json network_info;
};

class FloodClone {
private:
    ThreadPool thread_pool;
    std::unique_ptr<FileManager> file_manager;
    std::unique_ptr<ConnectionManager> connection_manager;
    Arguments args;

    void setup_node();
    void run_source();
    void run_destination();
    int get_port_from_network_info(const std::string& node_name);
    void write_timestamp();

public:
    FloodClone(const Arguments& args);
    void start();
};

#endif