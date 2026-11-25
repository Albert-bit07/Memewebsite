#include "user_store.hpp"
#include <filesystem>
#include <future>
#include <stdexcept>
#include <string>

void require(bool value) {
    if (!value) throw std::runtime_error("Shared storage assertion failed");
}

int main(int argc, char** argv) {
    require(argc == 3);
    const std::filesystem::path database(argv[2]);
    std::filesystem::remove(database);
    Recommender catalog;
    catalog.load_catalog(argv[1]);
    {
        UserStore first(database.string(), catalog), second(database.string(), catalog);
        const auto token = first.create_session();
        require(first.snapshot(token).liked.empty());
        require(second.snapshot(token).liked.empty());
        require(first.feedback(token, 0, "like") == 1);
        require(second.snapshot(token).liked.count(0) == 1);
        // A seen event must preserve a like committed through another connection.
        require(second.feedback(token, 0, "seen") == 1);
        require(first.feedback(token, 0, "unlike") == 0);
        require(second.snapshot(token).liked.empty());
        require(second.feedback(token, 0, "like") == 1);
        require(first.feedback(token, 0, "seen") == 1);
        require(first.snapshot(token).liked.count(0) == 1);
        second.delete_session(token);
        bool rejected = false;
        try { first.feedback(token, 0, "like"); }
        catch (const UnknownSession&) { rejected = true; }
        require(rejected);
        // A rejected transaction must release its write lock.
        require(!second.create_session().empty());
        const auto concurrent = first.create_session();
        for (int round = 0; round < 20; ++round) {
            for (const std::string action : {"like", "unlike"}) {
                std::promise<void> ready;
                auto start = ready.get_future().share();
                auto left = std::async(std::launch::async, [&] {
                    start.wait();
                    first.feedback(concurrent, 0, action);
                });
                auto right = std::async(std::launch::async, [&] {
                    start.wait();
                    second.feedback(concurrent, 1, action);
                });
                ready.set_value();
                left.get();
                right.get();
                const auto expected = action == "like" ? 2u : 0u;
                require(first.snapshot(concurrent).liked.size() == expected);
                require(second.snapshot(concurrent).liked.size() == expected);
            }
        }
    }
    std::filesystem::remove(database);
}
