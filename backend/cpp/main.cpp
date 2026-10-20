#include "httplib.h"
#include "json.hpp"
#include "recommender.hpp"
#include "user_store.hpp"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <regex>

using json = nlohmann::json;

std::string session_token(const httplib::Request& request) {
    const std::string cookie = request.get_header_value("Cookie");
    static const std::regex pattern("(?:^|;\\s*)meme_session=([a-f0-9]{64})(?:;|$)");
    std::smatch match;
    return std::regex_search(cookie,match,pattern) ? match[1].str() : "";
}
void respond(httplib::Response& response, const json& value) {
    response.set_header("Cache-Control","no-store");
    response.set_content(value.dump(),"application/json");
}
// Stable across process restarts; used only to diversify the cold-start order.
std::uint64_t order_key(const std::string& value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char byte : value) { hash ^= byte; hash *= 1099511628211ULL; }
    return hash;
}
int main(int argc, char** argv) {
    try {
        Recommender loaded;
        loaded.load_catalog(argc > 1 ? argv[1] : "data/catalog.json");
        const Recommender catalog = std::move(loaded);
        UserStore users(argc > 2 ? argv[2] : "data/users.sqlite3",catalog);
        httplib::Server server;
        server.set_payload_max_length(4096);
        server.set_mount_point("/",argc > 3 ? argv[3] : "../frontend");
        server.set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
            const auto origin = req.get_header_value("Origin");
            if ((!origin.empty() && origin != "http://127.0.0.1:18080" && origin != "http://localhost:18080") || req.get_header_value("Sec-Fetch-Site") == "cross-site") {
                res.status=403; respond(res,{{"error","Cross-site request rejected"}}); return httplib::Server::HandlerResponse::Handled;
            }
            if (req.path == "/recommend" || req.path == "/feedback" || req.path == "/status") {
                try { users.snapshot(session_token(req)); }
                catch (const std::invalid_argument&) { res.status=401; respond(res,{{"error","Create a session first"}}); return httplib::Server::HandlerResponse::Handled; }
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        server.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
            res.status=500; respond(res,{{"error","Request failed"}});
        });
        server.Post("/session",[&](const httplib::Request& req, httplib::Response& res) {
            auto token = session_token(req);
            try { users.snapshot(token); }
            catch (const std::invalid_argument&) {
                token = users.create_session();
                res.set_header("Set-Cookie","meme_session="+token+"; HttpOnly; SameSite=Strict; Path=/; Max-Age=2592000");
            }
            respond(res,{{"status","ok"}});
        });
        server.Get("/status",[&](const httplib::Request& req, httplib::Response& res) {
            const auto state=users.snapshot(session_token(req));
            respond(res,{{"status","ok"},{"catalog_version",catalog.version()},{"embeddings_loaded",catalog.get_embeddings().size()},
                {"paths_loaded",catalog.get_paths().size()},{"liked_count",state.liked.size()},{"liked_indices",state.liked}});
        });
        server.Post("/feedback",[&](const httplib::Request& req, httplib::Response& res) {
            try {
                const auto body=json::parse(req.body);
                if (!body.at("index").is_number_integer()) throw std::invalid_argument("Index must be an integer");
                const auto raw=body.at("index").get<std::int64_t>();
                if (raw<0 || raw>=static_cast<std::int64_t>(catalog.get_ids().size())) throw std::invalid_argument("Unknown item");
                const auto count=users.feedback(session_token(req),static_cast<int>(raw),body.at("action").get<std::string>());
                respond(res,{{"status","ok"},{"liked_count",count}});
            } catch (const json::exception&) { res.status=400; respond(res,{{"error","Invalid feedback"}}); }
              catch (const std::invalid_argument&) { res.status=400; respond(res,{{"error","Invalid item or action"}}); }
        });
        server.Get("/recommend",[&](const httplib::Request& req, httplib::Response& res) {
            const auto token=session_token(req);
            const auto state=users.snapshot(token);
            auto ranked=catalog.recommend(state.preference,state.liked.empty() ? static_cast<int>(catalog.get_ids().size()) : 20,state.seen);
            if (state.liked.empty()) {
                std::sort(ranked.begin(),ranked.end(),[&](int a,int b) {
                    const auto x=order_key(token+catalog.get_ids()[a]), y=order_key(token+catalog.get_ids()[b]);
                    return x==y ? a<b : x<y;
                });
            }
            json items=json::array();
            for (int index:ranked) {
                if (state.seen.count(index)) continue;
                items.push_back({{"index",index},{"id",catalog.get_ids()[index]},{"path",std::filesystem::path(catalog.get_paths()[index]).filename().string()}});
                if (items.size()==20) break;
            }
            respond(res,{{"recommendations",items}});
        });
        server.Get("/image/(\\d+)",[&](const httplib::Request& req, httplib::Response& res) {
            std::size_t index;
            try { index=std::stoull(req.matches[1]); } catch (...) { res.status=404; return; }
            if (index>=catalog.get_paths().size()) { res.status=404; return; }
            std::ifstream file(catalog.get_paths()[index],std::ios::binary);
            if (!file) { res.status=404; return; }
            const auto extension=std::filesystem::path(catalog.get_paths()[index]).extension().string();
            res.set_content(std::string(std::istreambuf_iterator<char>(file),{}),extension==".png" ? "image/png" : extension==".gif" ? "image/gif" : "image/jpeg");
        });
        std::cout << "Meme Feed: http://127.0.0.1:18080\n";
        return server.listen("127.0.0.1",18080) ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
