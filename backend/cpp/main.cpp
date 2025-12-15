#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <iostream>
#include <vector>
#include <string>
#include <mutex>
#include <fstream>

#include "httplib.h"
#include "json.hpp"

#include "recommender.hpp"
#include "utils.hpp"

using namespace pybind11::literals;
namespace py = pybind11;
using json = nlohmann::json;

Recommender recommender;
std::vector<int> liked_indices;
std::mutex mu;

int main(int argc, char** argv){
    std::cout << "Starting C++ Recommender (py embed)" << std::endl;
    
    // 1) Load embeddings from CSV + paths JSON
    std::string csv_path = "data/embeddings.csv";
    std::string paths_json = "data/meme_paths.json";
    recommender.load_embeddings(csv_path, paths_json);

    // 2) Initialize Python interpreter with error handling
    std::cout << "Initializing Python interpreter..." << std::endl;
    
    py::scoped_interpreter *guard = nullptr;
    py::module_ pyrec;
    
    try {
        guard = new py::scoped_interpreter();
        std::cout << "Python interpreter started" << std::endl;
        
        // Add current directory to Python path
        py::module_ sys = py::module_::import("sys");
        py::list path = sys.attr("path");
        path.append(".");
        path.append("python");
        path.append("../python");
        
        std::cout << "Importing py_recommender module..." << std::endl;
        pyrec = py::module_::import("py_recommender");
        std::cout << "Module imported successfully" << std::endl;
        
        // Call init_model
        std::cout << "Initializing CLIP model (this may take a minute)..." << std::endl;
        pyrec.attr("init_model")();
        std::cout << "Python CLIP model initialized." << std::endl;
        
    } catch(const py::error_already_set &e) {
        std::cerr << "Python error: " << e.what() << std::endl;
        return 1;
    } catch(const std::exception &e) {
        std::cerr << "Error initializing Python: " << e.what() << std::endl;
        return 1;
    }
    
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
        std::lock_guard<std::mutex> lock(mu);
        
        try {
            // Compute user vector by calling Python compute_user_pref
            py::list py_likes;
            for (int i : liked_indices) py_likes.append(i);
            
            py::object py_embeddings = py::cast(recommender.get_embeddings());
            py::object user_vec_obj = pyrec.attr("compute_user_pref")(py_likes, py_embeddings);
            std::vector<float> user_vec = user_vec_obj.cast<std::vector<float>>();

            // Get more recommendations (20 instead of 10)
            auto top = recommender.recommend(user_vec, 20);
            
            // Build JSON response with both path and index
            json response;
            response["recommendations"] = json::array();
            for (int idx : top) {
                json meme;
                meme["index"] = idx;
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
            
            {
                std::lock_guard<std::mutex> lock(mu);
                if(action == "like"){
                    liked_indices.push_back(idx);
                    std::cout << "User liked index: " << idx << std::endl;
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
        json response;
        response["status"] = "ok";
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
    
    svr.listen("127.0.0.1", 18080);
    
    if (guard) delete guard;
    return 0;
}