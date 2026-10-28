#include "request_admission.hpp"
#include <atomic>
#include <thread>
#include <vector>

void require(bool condition) {
    if (!condition) throw std::runtime_error("Session admission assertion failed");
}

int main() {
    using namespace std::chrono;
    const RequestAdmission::Clock::time_point start{};
    RequestAdmission limit(2);
    require(limit.retry_after(start) == 0);
    require(limit.retry_after(start + seconds(10)) == 0);
    require(limit.retry_after(start + milliseconds(10500)) == 50);
    require(limit.retry_after(start + seconds(59)) == 1);
    require(limit.retry_after(start + seconds(60)) == 0);
    require(limit.retry_after(start + seconds(60)) == 10);
    require(limit.retry_after(start + hours(1)) == 0);
    require(limit.retry_after(start + hours(1)) == 0);
    require(limit.retry_after(start + hours(1)) == 60);

    RequestAdmission shared(5);
    std::atomic<int> admitted{0};
    std::vector<std::thread> threads;
    for (int i=0; i<40; ++i) threads.emplace_back([&] {
        if (shared.retry_after(start) == 0) ++admitted;
    });
    for (auto& thread : threads) thread.join();
    require(admitted == 5);
    for (auto invalid : {0, 60001}) {
        bool rejected = false;
        try { RequestAdmission bad(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected);
    }
}
