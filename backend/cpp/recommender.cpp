#include "recommender.hpp"
#include "utils.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

Recommender::Recommender(){}
void Recommender::load_embeddings(const std::string &csv_path, const std::string &paths_json){
    embeddings= loadCSV(csv_path);
    paths = loadPathsJSON(paths_json);
    if (!embeddings.empty()){
        std::cout << "Loaded " << embeddings.size() << "embeddings. Dim=" << embeddings[0].size() << std::endl;   
    }else{
        std::cout << "No embeddings loaded."<< std::endl;
    }
}

float Recommender::cosine_similarity(const std::vector<float>& a, const std::vector<float>& b){
    float dot= 0.0f, na=0.0f, nb= 0.0f;
    int n = std::min(a.size(), b.size());
    for (int i=0; i<n; ++i){
        dot += a[i] *b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    if (na == 0 || nb == 0) return 0.0f;
    return dot/ (std::sqrt(na) * std::sqrt(nb));
}

std::vector<int> Recommender::recommend(const std::vector<float> &user_vec, int top_k){
    std::vector<std::pair<float,int>> scores;
    for (int i=0; i< embeddings.size(); ++i){
        float s = cosine_similarity(user_vec, embeddings[i]);
        scores.push_back({s,i});
    }
    std::sort(scores.begin(), scores.end(),[](auto &a, auto &b){return a.first >b.first;});
    std::vector<int> top;
    for (int i =0; i<std::min(top_k, (int)scores.size()); ++i) top.push_back(scores[i].second);
    return top;
}
const std::vector<std::string>& Recommender::get_paths() const{return paths;}
const std::vector<std::vector<float>>&  Recommender::get_embeddings() const{return embeddings; }
