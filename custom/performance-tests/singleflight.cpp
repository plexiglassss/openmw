#include <components/misc/singleflight.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace std::chrono_literals;
int main()
{
    Misc::SingleFlight<std::string, std::shared_ptr<int>> work;
    auto wait = [](const auto& f) { return f.get(); };
    std::promise<void> entered, release, joined;
    auto released = release.get_future().share();
    std::atomic<int> builds{0};
    auto producer = std::async(std::launch::async, [&] {
        return work.get("mesh", [&] {
            ++builds; entered.set_value(); released.wait();
            return std::make_shared<int>(42);
        }, wait);
    });
    entered.get_future().wait();
    auto consumer = std::async(std::launch::async, [&] {
        return work.get("mesh", [&] { ++builds; return std::make_shared<int>(0); },
            [&](const auto& f) { joined.set_value(); return f.get(); });
    });
    joined.get_future().wait();
    auto other = std::async(std::launch::async, [&] {
        return work.get("other", [] { return std::make_shared<int>(7); }, wait);
    });
    assert(other.wait_for(5s) == std::future_status::ready);
    assert(*other.get() == 7);
    release.set_value();
    auto a = producer.get(), b = consumer.get();
    assert(a == b && *a == 42 && builds == 1);
    // Completed values are not retained here: external cache/invalidation owns them.
    auto fresh = work.get("mesh", [] { return std::make_shared<int>(8); }, wait);
    assert(*fresh == 8 && fresh != a);

    std::promise<void> failureEntered, failNow, failureJoined;
    auto failureRelease = failNow.get_future().share();
    auto failure = std::async(std::launch::async, [&] {
        try { work.get("bad", [&]() -> std::shared_ptr<int> {
            failureEntered.set_value(); failureRelease.wait();
            throw std::runtime_error("load failed");
        }, wait); } catch (const std::runtime_error&) { return true; }
        return false;
    });
    failureEntered.get_future().wait();
    auto failureConsumer = std::async(std::launch::async, [&] {
        try { work.get("bad", [] { return std::make_shared<int>(0); },
            [&](const auto& f) { failureJoined.set_value(); return f.get(); });
        } catch (const std::runtime_error&) { return true; }
        return false;
    });
    failureJoined.get_future().wait();
    failNow.set_value();
    assert(failure.get() && failureConsumer.get());
    assert(*work.get("bad", [] { return std::make_shared<int>(9); }, wait) == 9);
    std::cout << "PASS: same-key sharing, independent-key progress, shared failure, retry, no retained results\n";
}
