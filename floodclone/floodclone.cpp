#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include <fstream>
#include "ThreadPool.h"
#include "FileManager.h"
#include "ConnectionManager.h"

using json = nlohmann::json;

struct Arguments {
   std::string mode;
   std::string node_name;
   std::string src_name;
   std::string file_path;
   std::string pieces_dir;
   std::string timestamp_file;
   json network_info;
};

Arguments parse_args(int argc, char* argv[]) {
   Arguments args;
   for(int i = 1; i < argc; i++) {
       std::string arg = argv[i];
       if(arg == "--mode") args.mode = argv[++i];
       else if(arg == "--node-name") args.node_name = argv[++i];
       else if(arg == "--src-name") args.src_name = argv[++i];
       else if(arg == "--file") args.file_path = argv[++i];
       else if(arg == "--pieces-dir") args.pieces_dir = argv[++i];
       else if(arg == "--timestamp-file") args.timestamp_file = argv[++i];
       else if(arg == "--network-info") args.network_info = json::parse(argv[++i]);
   }
   return args;
}

int main(int argc, char* argv[]) {
   try {
       auto args = parse_args(argc, argv);
       
       std::cout << "Mode: " << args.mode << std::endl;
       std::cout << "Node name: " << args.node_name << std::endl;
       std::cout << "Source name: " << args.src_name << std::endl;
       std::cout << "File path: " << args.file_path << std::endl;
       std::cout << "Pieces directory: " << args.pieces_dir << std::endl;
       std::cout << "Timestamp file: " << args.timestamp_file << std::endl;
       std::cout << "Network info: " << args.network_info.dump(2) << std::endl;
       
       return 0;
   } catch(const std::exception& e) {
       std::cerr << "Error: " << e.what() << std::endl;
       return 1;
   }
}