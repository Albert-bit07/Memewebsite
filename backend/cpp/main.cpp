#include "httplib.h"
#include "json.hpp"
#include "recommender.hpp"
#include "user_store.hpp"
#include "request_failure.hpp"
#include "session_admission.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <regex>
#include <unordered_map>
#include <shared_mutex>
#include <cstdlib>
#include <type_traits>

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
std::unordered_map<std::string, int> item_lookup(const Recommender& catalog) {
    std::unordered_map<std::string, int> indexes;
    for (std::size_t i = 0; i < catalog.get_ids().size(); ++i) {
        indexes.emplace(catalog.get_ids()[i], static_cast<int>(i));
    }
    return indexes;
}
int main(int argc, char** argv) {
    try {
        const auto manifest = std::filesystem::absolute(argc > 1 ? argv[1] : "data/catalog.json").string();
        const char* configured_token = std::getenv("MEME_ADMIN_TOKEN");
        const std::string admin_token = configured_token ? configured_token : "";
        if (!admin_token.empty() && admin_token.size() < 32) {
            throw std::runtime_error("MEME_ADMIN_TOKEN must contain at least 32 characters");
        }
        std::size_t max_sessions = 10000;
        if (const char* setting = std::getenv("MEME_MAX_SESSIONS")) {
            const std::string value(setting);
            const auto parsed = std::from_chars(value.data(), value.data()+value.size(), max_sessions);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() || max_sessions == 0 || max_sessions > 1000000) {
                throw std::runtime_error("MEME_MAX_SESSIONS must be an integer from 1 to 1000000");
            }
        }
        std::size_t creations_per_minute = 60;
        if (const char* setting = std::getenv("MEME_SESSION_CREATIONS_PER_MINUTE")) {
            const std::string value(setting);
            const auto parsed = std::from_chars(value.data(), value.data()+value.size(), creations_per_minute);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() || creations_per_minute == 0 || creations_per_minute > 60000) {
                throw std::runtime_error("MEME_SESSION_CREATIONS_PER_MINUTE must be an integer from 1 to 60000");
            }
        }
        SessionAdmission admission(creations_per_minute);
        Recommender catalog;
        catalog.load_catalog(manifest);
        auto item_indices = item_lookup(catalog);
        std::shared_mutex catalog_mutex;
        std::mutex reload_mutex;
        const auto item_index = [&](const std::string& id) {
            const auto found = item_indices.find(id);
            if (found == item_indices.end()) throw std::invalid_argument("Unknown item");
            return found->second;
        };
        UserStore users(argc > 2 ? argv[2] : "data/users.sqlite3",catalog,max_sessions);
        httplib::Server server;
        server.set_payload_max_length(4096);
        server.set_mount_point("/",argc > 3 ? argv[3] : "../frontend");
        server.set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
            const auto origin = req.get_header_value("Origin");
            if ((!origin.empty() && origin != "http://127.0.0.1:18080" && origin != "http://localhost:18080") || req.get_header_value("Sec-Fetch-Site") == "cross-site") {
                res.status=403; respond(res,{{"error","Cross-site request rejected"}}); return httplib::Server::HandlerResponse::Handled;
            }
            if (req.path == "/recommend" || req.path == "/feedback" || req.path == "/status") {
                std::shared_lock<std::shared_mutex> generation(catalog_mutex);
                try { users.snapshot(session_token(req)); }
                catch (const UnknownSession&) { res.status=401; respond(res,{{"error","Create a session first"}}); return httplib::Server::HandlerResponse::Handled; }
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        server.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr exception) {
            const auto failure = request_failure(exception);
            res.status=failure.status; respond(res,{{"error",failure.message}});
        });
        server.Post("/session",[&](const httplib::Request& req, httplib::Response& res) {
            std::shared_lock<std::shared_mutex> generation(catalog_mutex);
            auto token = session_token(req);
            try { users.snapshot(token); }
            catch (const UnknownSession&) {
                const int retry_after = admission.retry_after();
                if (retry_after > 0) {
                    res.status=429;
                    res.set_header("Retry-After",std::to_string(retry_after));
                    respond(res,{{"error","Session creation rate exceeded; retry later"}}); return;
                }
                try { token = users.create_session(); }
                catch (const SessionCapacityReached&) {
                    res.status=503; respond(res,{{"error","Session capacity reached; existing sessions remain available"}}); return;
                }
                res.set_header("Set-Cookie","meme_session="+token+"; HttpOnly; SameSite=Strict; Path=/; Max-Age=2592000");
            }
            respond(res,{{"status","ok"}});
        });
        server.Delete("/session",[&](const httplib::Request& req, httplib::Response& res) {
            users.delete_session(session_token(req));
            res.set_header("Set-Cookie","meme_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
            respond(res,{{"status","ok"}});
        });
        server.Get("/status",[&](const httplib::Request& req, httplib::Response& res) {
            std::shared_lock<std::shared_mutex> generation(catalog_mutex);
            const auto state=users.snapshot(session_token(req));
            std::vector<std::string> liked_ids;
            for (int index : state.liked) liked_ids.push_back(catalog.get_ids()[index]);
            std::sort(liked_ids.begin(), liked_ids.end());
            respond(res,{{"status","ok"},{"liked_ids",liked_ids},{"catalog_version",catalog.version()},{"embeddings_loaded",catalog.get_embeddings().size()},
                {"paths_loaded",catalog.get_paths().size()},{"liked_count",state.liked.size()},{"liked_indices",state.liked}});
        });
        server.Post("/feedback",[&](const httplib::Request& req, httplib::Response& res) {
            std::shared_lock<std::shared_mutex> generation(catalog_mutex);
            try {
                const auto body=json::parse(req.body);
                const auto index = item_index(body.at("id").get<std::string>());
                const auto count=users.feedback(session_token(req),index,body.at("action").get<std::string>());
                respond(res,{{"status","ok"},{"liked_count",count}});
            } catch (const json::exception&) { res.status=400; respond(res,{{"error","Invalid feedback"}}); }
              catch (const std::invalid_argument&) { res.status=400; respond(res,{{"error","Invalid item or action"}}); }
        });
        server.Get("/recommend",[&](const httplib::Request& req, httplib::Response& res) {
            std::shared_lock<std::shared_mutex> generation(catalog_mutex);
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
            respond(res,{{"recommendations",items},{"catalog_version",catalog.version()}});
        });
        server.Get("/image",[&](const httplib::Request& req, httplib::Response& res) {
            std::shared_lock<std::shared_mutex> generation(catalog_mutex);
            int index;
            try { index = item_index(req.get_param_value("id")); }
            catch (const std::invalid_argument&) { res.status=404; return; }
            std::ifstream file(catalog.get_paths()[index],std::ios::binary);
            if (!file) { res.status=404; return; }
            const auto extension=std::filesystem::path(catalog.get_paths()[index]).extension().string();
            res.set_content(std::string(std::istreambuf_iterator<char>(file),{}),extension==".png" ? "image/png" : extension==".gif" ? "image/gif" : "image/jpeg");
        });
        server.Post("/admin/reload",[&](const httplib::Request& req, httplib::Response& res) {
            if (admin_token.empty()) { res.status=404; return; }
            if (req.get_header_value("Authorization") != "Bearer " + admin_token) {
                res.status=401; respond(res,{{"error","Administrator token required"}}); return;
            }
            // Serialize preparation too, so an older candidate cannot publish
            // after a newer reload. Ordinary requests continue during loading.
            std::lock_guard<std::mutex> reload(reload_mutex);
            Recommender candidate;
            try { candidate.load_catalog(manifest); }
            catch (const std::exception&) {
                res.status=422; respond(res,{{"error","Catalog validation failed; previous catalog retained"}}); return;
            }
            auto indexes = item_lookup(candidate);
            std::unique_lock<std::shared_mutex> generation(catalog_mutex);
            // Drop only derived state. The next access reconstructs each user
            // from durable stable IDs using the new dimensions and vectors.
            users.clear_cached_preferences();
            static_assert(std::is_nothrow_move_assignable<Recommender>::value);
            catalog = std::move(candidate);
            item_indices.swap(indexes);
            respond(res,{{"status","ok"},{"catalog_version",catalog.version()}});
        });
        std::cout << "Meme Feed: http://127.0.0.1:18080\n";
        return server.listen("127.0.0.1",18080) ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
