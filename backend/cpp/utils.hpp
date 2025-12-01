#pragma once
#include <vector>
#include <string>

std::vector<std::vector<float>> loadCSV(const std::string &path);
std::vector<std::string> loadPathsJSON(const std::string &path);
std::string readFileToString(const std::string &path);

