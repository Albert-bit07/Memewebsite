#pragma once
#include <chrono>
#include <deque>
#include <mutex>
#include <stdexcept>

// One process-wide rolling window, with no attacker-controlled identity map.
class SessionAdmission {
public:
    using Clock = std::chrono::steady_clock;
    explicit SessionAdmission(std::size_t limit) : limit_(limit) {
        if (limit == 0 || limit > 60000) throw std::invalid_argument("Invalid session creation rate");
    }
    // Zero admits the attempt; otherwise return whole seconds until a slot opens.
    int retry_after() {
        std::lock_guard<std::mutex> lock(mutex_);
        return retry_after_locked(Clock::now());
    }
    int retry_after(Clock::time_point now) {
        std::lock_guard<std::mutex> lock(mutex_);
        return retry_after_locked(now);
    }
private:
    int retry_after_locked(Clock::time_point now) {
        while (!attempts_.empty() && now - attempts_.front() >= std::chrono::seconds(60)) {
            attempts_.pop_front();
        }
        if (attempts_.size() >= limit_) {
            return static_cast<int>(std::chrono::ceil<std::chrono::seconds>(
                attempts_.front() + std::chrono::seconds(60) - now).count());
        }
        attempts_.push_back(now);
        return 0;
    }
    const std::size_t limit_;
    std::mutex mutex_;
    std::deque<Clock::time_point> attempts_;
};
