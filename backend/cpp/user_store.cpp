#include "user_store.hpp"
#include <memory>
#include <stdexcept>

namespace {
class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) throw std::runtime_error("Cannot prepare user storage query");
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    void bind(int index, const std::string& text) {
        if (sqlite3_bind_text(stmt_, index, text.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) throw std::runtime_error("Cannot bind user storage value");
    }
    bool row() {
        const int result = sqlite3_step(stmt_);
        if (result != SQLITE_ROW && result != SQLITE_DONE) throw std::runtime_error("User storage query failed");
        return result == SQLITE_ROW;
    }
    std::string text(int column) const { return reinterpret_cast<const char*>(sqlite3_column_text(stmt_, column)); }
    int integer(int column) const { return sqlite3_column_int(stmt_, column); }
private:
    sqlite3_stmt* stmt_ = nullptr;
};
}

UserStore::UserStore(const std::string& path, const Recommender& catalog, std::size_t max_sessions) : catalog_(catalog), max_sessions_(max_sessions) {
    if (max_sessions == 0 || max_sessions > 1000000) throw std::invalid_argument("Invalid session capacity");
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) { sqlite3_close(db_); throw std::runtime_error("Cannot open user database"); }
    try {
        execute("PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA busy_timeout=5000;");
        execute("CREATE TABLE IF NOT EXISTS sessions(token TEXT PRIMARY KEY, created_at TEXT DEFAULT CURRENT_TIMESTAMP);"
                "CREATE TABLE IF NOT EXISTS preferences(session TEXT REFERENCES sessions(token), item_id TEXT, liked INTEGER NOT NULL, seen INTEGER NOT NULL, PRIMARY KEY(session,item_id));"
                "CREATE TABLE IF NOT EXISTS feedback_events(id INTEGER PRIMARY KEY, session TEXT REFERENCES sessions(token), item_id TEXT NOT NULL, action TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP);");
    } catch (...) { sqlite3_close(db_); throw; }
}
UserStore::~UserStore() { sqlite3_close(db_); }
void UserStore::execute(const char* sql) {
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK) throw std::runtime_error("User storage transaction failed");
}
void UserStore::clear_cached_preferences() {
    std::lock_guard<std::mutex> guard(mutex_);
    states_.clear();
}
std::string UserStore::create_session() {
    std::lock_guard<std::mutex> guard(mutex_);
    // Count and insert share a single SQLite write statement, including when
    // another connection uses this database. Existing sessions are never evicted.
    Statement insert(db_, "INSERT INTO sessions(token) SELECT lower(hex(randomblob(32))) WHERE (SELECT COUNT(*) FROM sessions) < CAST(? AS INTEGER) RETURNING token");
    insert.bind(1, std::to_string(max_sessions_));
    if (!insert.row()) throw SessionCapacityReached();
    return insert.text(0);
}
void UserStore::delete_session(const std::string& session) {
    std::lock_guard<std::mutex> guard(mutex_);
    // Children and bearer token disappear together; a failed commit leaves the
    // live cache usable and durable preferences intact for a retry.
    execute("BEGIN IMMEDIATE");
    try {
        for (const char* sql : {"DELETE FROM feedback_events WHERE session=?",
                                "DELETE FROM preferences WHERE session=?",
                                "DELETE FROM sessions WHERE token=?"}) {
            Statement remove(db_, sql);
            remove.bind(1, session);
            remove.row();
        }
        execute("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
    states_.erase(session);
}
UserStore::State& UserStore::load(const std::string& session) {
    Statement exists(db_, "SELECT 1 FROM sessions WHERE token=?"); exists.bind(1,session);
    if (session.size() != 64 || !exists.row()) throw std::invalid_argument("Unknown session");
    auto found = states_.find(session);
    if (found != states_.end()) return found->second;
    // Cache eviction only removes derived state; durable interactions remain in SQLite.
    if (states_.size() >= 1024) states_.clear();
    State state(catalog_.get_embeddings().front().size());
    std::unordered_map<std::string,int> indexes;
    for (int i=0; i<static_cast<int>(catalog_.get_ids().size()); ++i) indexes.emplace(catalog_.get_ids()[i],i);
    Statement query(db_, "SELECT item_id,liked,seen FROM preferences WHERE session=?"); query.bind(1,session);
    while (query.row()) {
        const auto item = indexes.find(query.text(0));
        if (item == indexes.end()) continue; // Removed items remain in history but not the current vector space.
        const int index = item->second;
        if (query.integer(1)) { state.liked.insert(index); state.preference.add(catalog_.get_embeddings()[index]); }
        if (query.integer(2)) state.seen.insert(index);
    }
    return states_.emplace(session,std::move(state)).first->second;
}
UserSnapshot UserStore::snapshot(const std::string& session) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto& state = load(session);
    return {state.preference.vector(),state.liked,state.seen};
}
std::size_t UserStore::feedback(const std::string& session, int index, const std::string& action) {
    if (index < 0 || index >= static_cast<int>(catalog_.get_ids().size()) ||
        (action != "like" && action != "unlike" && action != "seen" && action != "skip")) throw std::invalid_argument("Invalid item or action");
    std::lock_guard<std::mutex> guard(mutex_);
    auto& state = load(session);
    const bool was_liked = state.liked.count(index), was_seen = state.seen.count(index);
    const bool liked = action == "like" || (was_liked && action != "unlike");
    if (liked == was_liked && was_seen) return state.liked.size();
    const auto& id = catalog_.get_ids()[index];
    execute("BEGIN IMMEDIATE");
    try {
        Statement update(db_, "INSERT INTO preferences(session,item_id,liked,seen) VALUES(?,?,?,1) ON CONFLICT(session,item_id) DO UPDATE SET liked=excluded.liked,seen=1");
        update.bind(1,session); update.bind(2,id); update.bind(3,liked ? "1":"0"); update.row();
        Statement event(db_, "INSERT INTO feedback_events(session,item_id,action) VALUES(?,?,?)");
        event.bind(1,session); event.bind(2,id); event.bind(3,action); event.row();
        execute("COMMIT");
    } catch (...) { sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr); throw; }
    // Rebuild from durable state if allocation fails after commit.
    try {
        state.seen.insert(index);
        if (liked && !was_liked) { state.liked.insert(index); state.preference.add(catalog_.get_embeddings()[index]); }
        if (!liked && was_liked) { state.liked.erase(index); state.preference.remove(catalog_.get_embeddings()[index]); }
        return state.liked.size();
    } catch (...) { states_.erase(session); throw; }
}
