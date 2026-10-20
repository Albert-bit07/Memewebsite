#include "utils.hpp" 
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include "json.hpp"

using json = nlohmann::json;

std::vector<std::vector<float>> loadCSV(const std::string &path){
    std::ifstream file(path);
    std::vector<std::vector<float>> data;
    if (!file.is_open()){
        throw std::runtime_error("Cannot open embeddings CSV: " + path);
    }
    std::string line;
    while(std::getline(file, line)){
        if(line.empty()) continue;
        std::stringstream ss(line);
        std::string item;
        std::vector<float> row;
        while(std::getline(ss,item,',')){
            size_t consumed = 0;
            float value = std::stof(item, &consumed);
            if (consumed != item.size() || !std::isfinite(value)) {
                throw std::runtime_error("Invalid embedding value");
            }
            row.push_back(value);
        }
        if (row.empty() || line.back() == ',' || (!data.empty() && row.size() != data.front().size())) {
            throw std::runtime_error("Inconsistent embedding dimensions");
        }
        data.push_back(row);
    }
    return data;
}

std::vector<std::string> loadPathsJSON(const std::string &path){
    std::ifstream f(path);
    std::vector<std::string> res;
    if (!f.is_open()){
        throw std::runtime_error("Cannot open image manifest: " + path);
    }
    json j;
    f >> j;
    for (auto &it : j) res.push_back(it.get<std::string>());
    return res;
}
std::string readFileToString(const std::string &path){
    std::ifstream t(path);
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}
