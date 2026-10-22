#include <components/misc/boundedcache.hpp>

#include <atomic>
#include <barrier>
#include <future>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>

void check(bool condition)
{
    if (!condition)
        throw std::runtime_error("Reference cache test failed");
}

int main()
{
    using Value = std::map<int, int>;
    Misc::BoundedCache<int, Value> cache(2);
    std::atomic_int loads{ 0 };
    std::barrier start(8);
    std::vector<std::jthread> workers;
    std::vector<std::shared_ptr<const Value>> results(8);
    for (int i = 0; i < 8; ++i)
        workers.emplace_back([&, i] {
            start.arrive_and_wait();
            results[i] = cache.get(1, [&] {
                ++loads;
                return Value{ { 1, 20 }, { 2, 30 } };
            });
        });
    workers.clear();
    check(loads == 1);
    for (const auto& result : results)
        check(result == results.front());
    // Per-chunk blacklisting must not mutate the shared resolved input.
    auto filtered = *results.front();
    filtered.erase(1);
    check(results.front()->size() == 2);

    cache.get(2, [] { return Value{}; });
    cache.get(3, [] { return Value{}; });
    cache.get(1, [&] {
        ++loads;
        return Value{};
    });
    check(loads == 2); // FIFO eviction
    check(results.front()->at(1) == 20); // evicted callers retain valid data

    cache.clear();
    int attempts = 0;
    try
    {
        cache.get(7, [&]() -> Value {
            ++attempts;
            throw std::runtime_error("read failed");
        });
        check(false);
    }
    catch (const std::runtime_error&)
    {
    }
    const auto recovered = cache.get(7, [&] {
        ++attempts;
        return Value{ { 5, 6 } };
    });
    check(attempts == 2 && recovered->at(5) == 6);

    // A loader for another cell can complete while the first cell is blocked.
    cache.clear();
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    auto oldLoad = std::async(std::launch::async, [&] {
        return cache.get(1, [&] {
            entered.set_value();
            gate.wait();
            return Value{ { 1, 10 } };
        });
    });
    entered.get_future().wait();
    auto other = std::async(std::launch::async, [&] { return cache.get(2, [] { return Value{}; }); });
    const bool independent = other.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    // Clear during a load must not let that old result repopulate the cache.
    cache.clear();
    const auto fresh = cache.get(1, [] { return Value{ { 1, 99 } }; });
    release.set_value();
    check(independent);
    check(oldLoad.get()->at(1) == 10);
    check(fresh->at(1) == 99);
    check(cache.get(1, [] { return Value{}; }) == fresh);
    other.get();
    std::cout << "Reference cache tests passed\n";
}
