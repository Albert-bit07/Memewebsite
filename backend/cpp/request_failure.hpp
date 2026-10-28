#pragma once
#include <exception>
#include "user_store.hpp"

struct RequestFailure {
    int status;
    const char* message;
};

inline RequestFailure request_failure(std::exception_ptr error) {
    try {
        if (error) std::rethrow_exception(error);
    } catch (const UnknownSession&) {
        return {401, "Create a session first"};
    } catch (...) {
        // Storage and internal exception details must not reach the client.
    }
    return {500, "Request failed"};
}
