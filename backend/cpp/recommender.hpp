#pragma once
#include <vector>
#include <string>
#include <unordered_set>

class Recommender{
public:
    Recommender();
    void load_catalog(const std::string& manifest);
    const std::vector<std::string>& get_ids() const { return ids; }
    const std::string& version() const { return catalog_version; }
    void load_embeddings(const std::string &csv_path, const std::string &paths_json);
    std::vector<int> recommend(const std::vector<float> &user_vec, int top_k=10, const std::unordered_set<int>& excluded = {}) const;
    const std::vector<std::string>& get_paths() const;
    const std::vector<std::vector<float>>& get_embeddings() const;

private:
    std::vector<std::vector<float>> embeddings;
    std::vector<std::string> paths;
    std::vector<std::string> ids;
    std::string catalog_version;
    float cosine_similarity(const std::vector<float>& a , const std::vector<float>& b) const;
};
