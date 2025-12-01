#pragma once
#include <vector>
#include <string>

class Recommender{
public:
    Recommender();
    void load_embeddings(const std::string &csv_path, const std::string &paths_json);
    std::vector<int> recommend(const std::vector<float> &user_vec, int top_k=10);
    const std::vector<std::string>& get_paths() const;
    const std::vector<std::vector<float>>& get_embeddings() const;

private:
    std::vector<std::vector<float>> embeddings;
    std::vector<std::string>> paths;
    float cosine_similarity(const std::vector<float>& a , const std::vector<float>& b);
};
