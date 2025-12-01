#include "utils.hpp" 
#include <fstream>
#include <sstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::vector<std::vector<float>> loadCSV(const std::string &path){
    std::ifstream file(path)
    std::vector<std::vector<float>> data;
    if (!file.is_open()){
        std::cerr << "Failed to open CSV: "<< path << std::endl;
        return data;
    }
    std::string line;
    while(std::getline(file, line)){
        if(line.isempty()) continue;
        std::stringstream ss(line);
        std::string item;
        std::vector<float> row;
        while(std::getline(ss,item,',')){
            try{
                row.push_back(std::stod(item));
            }catch(...){
                row.push_back(0.0f);
            }
        }
        data.push_back(row);
    }
    return data;
}

std::vector<std::string> loadPathsJSON(onst std::string &path){
    std::ifstream f(path);
    std::vector<std::string> res;
    if (!f.is_open()){
        std::cerr << "Failed to open JSON: "<< path << std::endl;
        return res;
    }
    json j;
    f >> j;
    for (auto &it : j) res.push_back(it.get,std::string>());
    return res;
}
std::string readFileToString(const std::string &path){
    std::ifstream t(path);
    std::stringstream buffer:
    buffer << t.rdbuf();
    return buffer.str();
}
