#include <pybind11/embed.h>
#include <pybind11/stl.h>
# include <iostream>
#include <vector>
#include <string>
#include <mutex>

#include "recommender.hpp"
#include "utils.hpp"

// Simple header-only HTTP using Crow is optional to keep dependencues minimal we'll implement a tiny HTTP using civeweb or suggest using "crow_all.h"
// For clarity in this project, we'll include a minimalistic single threaded JSON-over-stdin example server using CivetWeb would be heavier.
// Instead, to keep this short and portable, we'll implement a basic HTTP using the crow.h header. you'll need crow_all.h placed in cpp/.

#include "crow_all.h" // get this and place in cpp/

using namespace pybind11::literals;
namespace py = pybind11;

Recommender recommender;
std::vector<int> liked_indices; // in-memory likes(per-sessions simplify)
std::mutex mu;

int main(int argc, char** argv){
    std::cout << "Starting C++ Recommennder (py embed)" << std::endl;
    //1) load embeddings from CSV + oaths JSON
    std::string csv_path = "data/embeddings.csv"; //produced by Python embedding step
    std::string paths_json = "data/meme_paths.json";
    recommender.load_embeddings(csv_path,paths_json);

    //2) Initialize Python interpreter and import our module
    py::scoped_interpreter guard{}; // start the interpreter
    py::module pyrec = py::module::import("py_recommender");
    //call init_model (this may load heavy model)
    try{
        pyrec.attr("init_model")();
        std::cout << "Python CLIP model initialized."<< std::endl;
    }catch(const std::excpetion &e){
        std::cerr << "Warning: init_model failed: "<< e.what() << std:endl;
        std::cerr << "Make sure to run 'pip install torch torchvision git+https://github.com/openai/CLIP.git' and have a working Python environment."<<std::endl;
    }
    //3) Start HTTP server using Crow
    crow::SimpleApp app;

    CROW_ROUTE(app, "/recommend")([
    &](const crow::request& req){
        std::lock_guard<std::mutex> lock(mu);
        //compute user vector by calling Python compute_user_pref(liked_indices, embeddings)
        py::list py_likes;
        for (int i: liked_indices) py_likes.append(i);
        py::object py_embeddings = py::cast(recommender.get_ebeddings());
        py::object user_vec_obj = pyrec.attr("compute_user_pref")(py_likes, py_embeddings);
        std::vectr<float> user_vec = user_vec_obj.cast<std::vector<float>>();

        auto top = recommender.recommend(user_vec, 10);
        //convert to paths
        py::list out;
        for (int idx : top)out.append(recommender.get_paths()[idx]);
        crow::json::wvalue res;
        res["recommendations"] = crow::json::wvalue::list();
        for (auto &p : out) res["recommendations"].push_back(std::string(py::str(p)));
        return crow::response{res};
    });

    CROW_ROUTE(app, "/feedback").methods("POST")([
    &](const crow::requests& req){
        auto body = crow::json::load(req.body);
        if(!body) return crow::response(400);
        std::string action = body["action"].s(); //"like" or "skip"
        int idx = body ["index"].i();
        {
            std::lock_guard<std::mutex> lock(mu);
            if(action == "like"){
                liked_indices.push_back(idx);
            }else if(action == "skip"){
                //optionally record skips
            }
        }
        return crow::response(200);
    });

    std::cout << "Server running on https://127.0.0.1:18080" << std::endl;
    app.port(18080).multithreaded().run();
    return 0;
}
