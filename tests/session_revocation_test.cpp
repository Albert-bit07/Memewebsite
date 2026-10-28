#include "user_store.hpp"
#include "request_failure.hpp"
#include <stdexcept>
#include <string>

void require(bool value) {
    if (!value) throw std::runtime_error("Session revocation assertion failed");
}

int main(int argc, char** argv) {
    require(argc == 2);
    Recommender catalog;
    catalog.load_catalog(argv[1]);
    UserStore users(":memory:", catalog);
    for (bool feedback : {false, true}) {
        const auto token = users.create_session();
        users.snapshot(token); // Pre-routing authentication succeeded.
        users.delete_session(token); // Revocation commits before handler access.
        bool rejected = false;
        try {
            if (feedback) users.feedback(token, 0, "like");
            else users.snapshot(token);
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("Revocation confused with invalid item/action");
        } catch (...) {
            const auto error = request_failure(std::current_exception());
            require(error.status == 401);
            require(std::string(error.message) == "Create a session first");
            rejected = true;
        }
        require(rejected);
    }
    const auto internal = request_failure(std::make_exception_ptr(std::runtime_error("private database path")));
    require(internal.status == 500);
    require(std::string(internal.message) == "Request failed");
    const auto token = users.create_session();
    bool invalid = false;
    try { users.feedback(token, 0, "invalid"); }
    catch (const std::invalid_argument&) { invalid = true; }
    require(invalid);
    require(users.snapshot(token).liked.empty());
}
