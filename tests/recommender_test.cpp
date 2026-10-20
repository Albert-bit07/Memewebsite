#include "recommender.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void require(bool condition) { if (!condition) throw std::runtime_error("assertion failed"); }
template<class F> void rejects(F f) {
    bool caught = false;
    try { f(); } catch (const std::exception&) { caught = true; }
    require(caught);
}
int main() {
    auto dir = std::filesystem::temp_directory_path() / "meme-recommender-test";
    std::filesystem::create_directories(dir);
    auto csv = (dir / "vectors.csv").string();
    auto paths = (dir / "paths.json").string();
    std::ofstream(csv) << "1,0\n0,1\n";
    std::ofstream(paths) << "[\"a.jpg\",\"b.jpg\"]";
    Recommender r;
    r.load_embeddings(csv, paths);
    require(r.recommend({0,1}, 1) == std::vector<int>{1});
    require(r.recommend({0,0}, 2) == std::vector<int>({0,1}));
    rejects([&] { r.recommend({1}, 1); });
    rejects([&] { r.recommend({1,0}, -1); });
    std::ofstream(csv) << "1,bad\n0,1\n";
    rejects([&] { r.load_embeddings(csv, paths); });
    require(r.recommend({0,1}, 1) == std::vector<int>{1});
    std::ofstream(csv) << "1,nan\n0,1\n";
    rejects([&] { r.load_embeddings(csv, paths); });
    std::ofstream(csv) << "1,0\n";
    rejects([&] { r.load_embeddings(csv, paths); });
    std::filesystem::remove_all(dir);
    std::cout << "Ranking, boundaries and atomic-load regressions passed\n";
}
