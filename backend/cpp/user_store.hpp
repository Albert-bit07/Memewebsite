#pragma once
#include "preference.hpp"
#include "recommender.hpp"
#include <sqlite3.h>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

struct UserSnapshot {
    std::vector<float> preference;
    std::unordered_set<int> liked;
    std::unordered_set<int> seen;
};

class SessionCapacityReached : public std::runtime_error {
public:
    SessionCapacityReached() : std::runtime_error("Session capacity reached") {}
};

class UserStore {
public:
    UserStore(const std::string& path, const Recommender& catalog, std::size_t max_sessions = 10000);
    ~UserStore();
    UserStore(const UserStore&) = delete;
    UserStore& operator=(const UserStore&) = delete;
    // Caller must exclude catalog readers until replacement is complete.
    void clear_cached_preferences();
    std::string create_session();
    void delete_session(const std::string& session);
    UserSnapshot snapshot(const std::string& session);
    std::size_t feedback(const std::string& session, int index, const std::string& action);
private:
    struct State {
        explicit State(std::size_t dimension) : preference(dimension) {}
        Preference preference;
        std::unordered_set<int> liked, seen;
    };
    State& load(const std::string& session);
    void execute(const char* sql);
    sqlite3* db_ = nullptr;
    const Recommender& catalog_;
    const std::size_t max_sessions_;
    std::mutex mutex_;
    std::unordered_map<std::string, State> states_;
};
