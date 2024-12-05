#ifndef COMMUNICATION_LITERALS_H
#define COMMUNICATION_LITERALS_H

#include <map>
#include <vector>

struct RouteInfo {
        std::string interface;
        int hop_count;
        std::vector<std::string> path;
    };

using NetworkMap = std::unordered_map<std::string, 
    std::unordered_map<std::string, std::vector<RouteInfo>>>;
using IpMap = std::unordered_map<std::string, 
    std::unordered_map<std::string, std::string>>;


#endif //COMMUNICATION_LITERALS_H