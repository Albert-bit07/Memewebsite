#include <iostream>
#include <vector>
#include <string>
#include <mutex>
#include <fstream>
#include <algorithm>

#include "httplib.h"
#include "json.hpp"

#include "recommender.hpp"
#include "preference.hpp"
#include "utils.hpp"

using json = nlohmann::json;

Recommender recommender;
std::vector<int> liked_indices;
std::mutex mu;

int main(int argc, char** argv){
    recommender.load_catalog(argc > 1 ? argv[1] : "data/catalog.json");
    Preference preference(recommender.get_embeddings().front().size());

    // 3) Start HTTP server using cpp-httplib
    httplib::Server svr;

    // CORS preflight handler for all routes
    svr.Options(".*", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // GET /recommend - Get recommendations based on liked memes
    svr.Get("/recommend", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::vector<float> user_vec;
            {
                std::lock_guard<std::mutex> lock(mu);
                user_vec = preference.vector();
            }
            // The immutable catalog is shared; scoring does not hold the user-state lock.
            auto top = recommender.recommend(user_vec, 20);

            // Build JSON response with both path and index
            json response;
            response["recommendations"] = json::array();
            for (int idx : top) {
                json meme;
                meme["index"] = idx;
                meme["id"] = recommender.get_ids()[idx];
                meme["path"] = recommender.get_paths()[idx];
                response["recommendations"].push_back(meme);
            }
            
            res.set_content(response.dump(), "application/json");
            res.set_header("Access-Control-Allow-Origin", "*");
        } catch(const std::exception &e) {
            json error;
            error["error"] = e.what();
            res.set_content(error.dump(), "application/json");
            res.status = 500;
        }
    });

    // POST /feedback - Record user feedback (like/skip)
    svr.Post("/feedback", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string action = body["action"];
            int idx = body["index"];
            
            std::lock_guard<std::mutex> lock(mu);
            if (idx < 0 || idx >= static_cast<int>(recommender.get_embeddings().size()) ||
                (action != "like" && action != "unlike" && action != "skip")) {
                throw std::invalid_argument("Invalid feedback index or action");
            }
            {
                if(action == "like"){
                    if (std::find(liked_indices.begin(), liked_indices.end(), idx) == liked_indices.end()) {
                        liked_indices.push_back(idx);
                        preference.add(recommender.get_embeddings()[idx]);
                    }
                    std::cout << "User liked index: " << idx << std::endl;
                } else if(action == "unlike") {
                    if (std::find(liked_indices.begin(), liked_indices.end(), idx) != liked_indices.end()) preference.remove(recommender.get_embeddings()[idx]);
                    liked_indices.erase(std::remove(liked_indices.begin(), liked_indices.end(), idx), liked_indices.end());
                } else if(action == "skip"){
                    std::cout << "User skipped index: " << idx << std::endl;
                }
            }
            
            json response;
            response["status"] = "ok";
            response["liked_count"] = liked_indices.size();
            
            res.set_content(response.dump(), "application/json");
            res.set_header("Access-Control-Allow-Origin", "*");
        } catch(const std::exception &e) {
            json error;
            error["error"] = e.what();
            res.set_content(error.dump(), "application/json");
            res.status = 400;
        }
    });

    // GET /status - Check server status
    svr.Get("/status", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mu);
        json response;
        response["status"] = "ok";
        response["catalog_version"] = recommender.version();
        response["embeddings_loaded"] = recommender.get_embeddings().size();
        response["paths_loaded"] = recommender.get_paths().size();
        response["liked_count"] = liked_indices.size();
        
        res.set_content(response.dump(), "application/json");
        res.set_header("Access-Control-Allow-Origin", "*");
    });

    // GET /memes - Get all meme paths
    svr.Get("/memes", [&](const httplib::Request& req, httplib::Response& res) {
        json response;
        response["memes"] = json::array();
        
        const auto& paths = recommender.get_paths();
        for (size_t i = 0; i < paths.size(); i++) {
            json meme;
            meme["index"] = i;
            meme["path"] = paths[i];
            response["memes"].push_back(meme);
        }
        
        res.set_content(response.dump(), "application/json");
        res.set_header("Access-Control-Allow-Origin", "*");
    });

    // GET /image/:index - Serve meme image by index
    svr.Get("/image/(\\d+)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int index = std::stoi(req.matches[1]);
            const auto& paths = recommender.get_paths();
            
            std::cout << "Image request for index: " << index << " (total paths: " << paths.size() << ")" << std::endl;
            
            if (index < 0 || index >= (int)paths.size()) {
                std::cout << "Index out of range" << std::endl;
                res.status = 404;
                res.set_content("Index out of range", "text/plain");
                return;
            }
            
            std::string file_path = paths[index];
            std::cout << "Trying to load: " << file_path << std::endl;
            
            std::ifstream file(file_path, std::ios::binary);
            
            if (!file.is_open()) {
                std::cout << "Failed to open file" << std::endl;
                res.status = 404;
                res.set_content("File not found", "text/plain");
                return;
            }
            
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            
            // Determine content type from extension
            std::string content_type = "image/jpeg";
            if (file_path.find(".png") != std::string::npos) {
                content_type = "image/png";
            } else if (file_path.find(".gif") != std::string::npos) {
                content_type = "image/gif";
            } else if (file_path.find(".jpg") != std::string::npos) {
                content_type = "image/jpeg";
            }
            
            std::cout << "Serving " << content.size() << " bytes as " << content_type << std::endl;
            
            res.set_content(content, content_type);
            res.set_header("Access-Control-Allow-Origin", "*");
            
        } catch (const std::exception& e) {
            std::cout << "Exception: " << e.what() << std::endl;
            res.status = 500;
            res.set_content(e.what(), "text/plain");
        }
    });

    std::cout << "Server running on http://127.0.0.1:18080" << std::endl;
    std::cout << "Try: curl http://127.0.0.1:18080/status" << std::endl;
    
    if (!svr.listen("127.0.0.1", 18080)) return 1;
    return 0;
}
