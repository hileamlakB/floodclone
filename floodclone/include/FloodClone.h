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
    nlohmann::json ip_map;
};

Arguments parse_args(int argc, char* argv[]);

class FloodClone {
private:
    ThreadPool thread_pool;
    std::unique_ptr<FileManager> file_manager;
    std::unique_ptr<ConnectionManager> connection_manager;
    Arguments args;
    std::chrono::system_clock::time_point start_time;  // New: Start time
    std::chrono::system_clock::time_point end_time;

    

    std::mutex node_mtx;
    size_t completed_nodes_ = 0;
    std::condition_variable node_change;
    size_t total_nodes_;

    static constexpr int COMPLETION_PORT = 9090;
    int completion_socket_;
    std::thread completion_thread_;
    bool is_listening_{true};

    std::string my_ip;

    
    // Map structure: node -> interface -> IP
    std::unordered_map<std::string, 
        std::unordered_map<std::string, std::string>> ip_map;
    
    // Map structure: source -> destination -> [(interface, hop_count, path)]
    struct RouteInfo {
        std::string interface;
        int hop_count;
        std::vector<std::string> path;
    };
    std::unordered_map<std::string, 
    std::unordered_map<std::string, std::vector<RouteInfo>>> network_map;

    void setup_node();
    void setup_net_info();
    std::string get_ip(const std::string& node_name);
    void record_time();
    void setup_completion();
    void listen_for_completion();
    void notify_completion();
    std::string find_immediate_neighbor(const std::string& target_node);

public:
    FloodClone(const Arguments& args);
    void start();
};



#endif