#include "json.hpp"
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include "recommender.hpp"
#include "utils.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

Recommender::Recommender(){}
void Recommender::load_embeddings(const std::string &csv_path, const std::string &paths_json){
    auto new_embeddings = loadCSV(csv_path);
    auto new_paths = loadPathsJSON(paths_json);
    if (new_embeddings.empty() || new_embeddings.size() != new_paths.size()) {
        throw std::runtime_error("Embeddings and image paths must have equal nonzero counts");
    }
    embeddings = std::move(new_embeddings);
    paths = std::move(new_paths);
    if (!embeddings.empty()){
        std::cout << "Loaded " << embeddings.size() << "embeddings. Dim=" << embeddings[0].size() << std::endl;   
    }else{
        std::cout << "No embeddings loaded."<< std::endl;
    }
}

float Recommender::cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) const {
    double dot= 0.0, na=0.0, nb=0.0;
    int n = std::min(a.size(), b.size());
    for (int i=0; i<n; ++i){
        dot += double(a[i]) * b[i];
        na += double(a[i]) * a[i];
        nb += double(b[i]) * b[i];
    }
    if (na == 0 || nb == 0) return 0.0f;
    return dot/ (std::sqrt(na) * std::sqrt(nb));
}

std::vector<int> Recommender::recommend(const std::vector<float> &user_vec, int top_k, const std::unordered_set<int>& excluded) const {
    if (top_k < 0 || embeddings.empty() || user_vec.size() != embeddings.front().size() ||
        !std::all_of(user_vec.begin(), user_vec.end(), [](float x) { return std::isfinite(x); })) {
        throw std::invalid_argument("Invalid recommendation request");
    }
    std::vector<std::pair<float,int>> scores;
    for (int i=0; i< embeddings.size(); ++i){
        if (excluded.count(i)) continue;
        float s = cosine_similarity(user_vec, embeddings[i]);
        scores.push_back({s,i});
    }
    const auto selected = std::min(static_cast<std::size_t>(top_k), scores.size());
    const auto better = [](const auto& a, const auto& b) { return a.first == b.first ? a.second < b.second : a.first > b.first; };
    if (selected == scores.size()) std::sort(scores.begin(), scores.end(), better);
    else std::partial_sort(scores.begin(), scores.begin() + selected, scores.end(), better);
    std::vector<int> top;
    for (int i =0; i<std::min(top_k, (int)scores.size()); ++i) top.push_back(scores[i].second);
    return top;
}
const std::vector<std::string>& Recommender::get_paths() const{return paths;}
const std::vector<std::vector<float>>&  Recommender::get_embeddings() const{return embeddings; }

void Recommender::load_catalog(const std::string& manifest) {
    namespace fs = std::filesystem;
    std::ifstream file(manifest);
    if (!file) throw std::runtime_error("Cannot open catalog manifest");
    const auto doc = nlohmann::json::parse(file);
    if (!doc.at("schema_version").is_number_integer() || doc.at("schema_version") != 1 || doc.at("catalog_version").get<std::string>().empty() || doc.at("model_version").get<std::string>().empty()) {
        throw std::runtime_error("Unsupported or incomplete catalog metadata");
    }
    if (!doc.at("dimension").is_number_integer() || doc.at("dimension") < 1 ||
        !doc.at("items").is_array()) {
        throw std::runtime_error("Catalog dimension must be a positive integer and items must be an array");
    }
    const auto root = fs::canonical(fs::absolute(manifest).parent_path());
    auto artifact_path = [&](const std::string& value) {
        fs::path relative(value);
        if (relative.empty() || relative.is_absolute()) throw std::runtime_error("Catalog paths must be relative");
        for (const auto& part : relative) if (part == "..") throw std::runtime_error("Catalog paths cannot escape artifact root");
        // Resolve links before checking containment; lexical '..' checks alone
        // allow an artifact to reference files outside its root through a link.
        const auto resolved = fs::canonical(root / relative);
        const auto within_root = resolved.lexically_relative(root);
        if (within_root.empty() || *within_root.begin() == "..") {
            throw std::runtime_error("Catalog paths cannot escape artifact root");
        }
        return resolved.string();
    };
    Recommender next;
    next.embeddings = loadCSV(artifact_path(doc.at("vectors")));
    if (next.embeddings.empty() || doc.at("dimension") != next.embeddings.front().size()) throw std::runtime_error("Catalog dimension mismatch");
    std::unordered_set<std::string> seen;
    for (const auto& item : doc.at("items")) {
        auto id = item.at("id").get<std::string>();
        if (id.empty() || !seen.insert(id).second) throw std::runtime_error("Catalog IDs must be unique and nonempty");
        auto path = artifact_path(item.at("path"));
        if (!fs::is_regular_file(path)) throw std::runtime_error("Catalog image missing");
        next.ids.push_back(std::move(id));
        next.paths.push_back(std::move(path));
    }
    if (next.paths.size() != next.embeddings.size()) throw std::runtime_error("Catalog item/vector count mismatch");
    next.catalog_version = doc.at("catalog_version");
    // Publish only after every row and referenced file has been validated.
    *this = std::move(next);
}
