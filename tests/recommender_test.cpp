#include "recommender.hpp"
#include "preference.hpp"
#include "json.hpp"
#include <cmath>
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
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto csv = (dir / "vectors.csv").string();
    auto paths = (dir / "paths.json").string();
    std::ofstream(csv) << "1,0\n0,1\n";
    std::ofstream(paths) << "[\"a.jpg\",\"b.jpg\"]";
    Recommender r;
    r.load_embeddings(csv, paths);
    require(r.recommend({0,1}, 1) == std::vector<int>{1});
    require(r.recommend({1,0}, 1, {0}) == std::vector<int>{1});
    require(r.recommend({1,0}, 0).empty());
    require(r.recommend({1,0}, 3, {0,1}).empty());
    require(r.recommend({0,0}, 2) == std::vector<int>({0,1}));
    rejects([&] { r.recommend({1}, 1); });
    rejects([&] { r.recommend({1,0}, -1); });
    std::ofstream(csv) << "1,0\r\n0,1\r\n";
    r.load_embeddings(csv, paths);
    require(r.recommend({0,1}, 1) == std::vector<int>{1});
    std::ofstream(csv) << "1,bad\n0,1\n";
    rejects([&] { r.load_embeddings(csv, paths); });
    require(r.recommend({0,1}, 1) == std::vector<int>{1});
    std::ofstream(csv) << "1,nan\n0,1\n";
    rejects([&] { r.load_embeddings(csv, paths); });
    std::ofstream(csv) << "1,0\n";
    rejects([&] { r.load_embeddings(csv, paths); });
    Preference preference(2);
    require(preference.vector() == std::vector<float>({0,0}));
    preference.add({1,0}); preference.add({0,1});
    require(std::abs(preference.vector()[0] - std::sqrt(0.5)) < 0.00001);
    preference.remove({1,0});
    require(preference.vector() == std::vector<float>({0,1}));
    preference.remove({0,1});
    require(preference.vector() == std::vector<float>({0,0}));
    rejects([&] { preference.remove({0,1}); });
    rejects([&] { preference.add({1}); });
    std::ofstream(csv) << "1,0\n0,1\n";
    std::ofstream(dir / "a.jpg") << "image-a";
    std::ofstream(dir / "b.jpg") << "image-b";
    nlohmann::json manifest = {{"schema_version",1},{"catalog_version","test-v1"},{"model_version","synthetic-test"},{"dimension",2},{"vectors","vectors.csv"},
        {"items", {{{"id","a"},{"path","a.jpg"}},{{"id","b"},{"path","b.jpg"}}}}};
    auto manifest_path = (dir / "catalog.json").string();
    std::ofstream(manifest_path) << manifest;
    r.load_catalog(manifest_path);
    require(r.get_ids() == std::vector<std::string>({"a","b"}));
    for (const auto& field : {"dimension", "schema_version"}) {
        auto invalid = manifest; invalid[field] = 99;
        std::ofstream(manifest_path) << invalid;
        rejects([&] { r.load_catalog(manifest_path); });
        require(r.version() == "test-v1");
        require(r.recommend({0,1},1) == std::vector<int>{1});
    }
    auto invalid = manifest; invalid["items"][1]["id"] = "a";
    std::ofstream(manifest_path) << invalid;
    rejects([&] { r.load_catalog(manifest_path); });
    invalid = manifest; invalid["items"][0]["path"] = "../escape.jpg";
    std::ofstream(manifest_path) << invalid;
    rejects([&] { r.load_catalog(manifest_path); });
    require(r.version() == "test-v1");
    // A relative name must not escape the artifact through a symlink.
    const auto outside = dir.parent_path() / "meme-recommender-test-outside";
    std::filesystem::create_directories(outside);
    std::ofstream(outside / "vectors.csv") << "0,1\n1,0\n";
    std::ofstream(outside / "private.jpg") << "outside-image";
    std::filesystem::create_directory_symlink(outside, dir / "external");
    for (const bool vectors : {true, false}) {
        invalid = manifest;
        if (vectors) invalid["vectors"] = "external/vectors.csv";
        else invalid["items"][0]["path"] = "external/private.jpg";
        std::ofstream(manifest_path) << invalid;
        rejects([&] { r.load_catalog(manifest_path); });
        require(r.version() == "test-v1");
        require(r.get_ids() == std::vector<std::string>({"a","b"}));
        require(r.recommend({0,1},1) == std::vector<int>{1});
    }
    std::filesystem::create_symlink(dir / "a.jpg", dir / "internal.jpg");
    auto internal = manifest;
    internal["items"][0]["path"] = "internal.jpg";
    std::ofstream(manifest_path) << internal;
    r.load_catalog(manifest_path);
    require(r.get_paths()[0] == std::filesystem::canonical(dir / "a.jpg").string());
    std::filesystem::remove_all(outside);
    std::filesystem::remove_all(dir);
    std::cout << "Ranking, boundaries and atomic-load regressions passed\n";
}
