#include <atomic>
#include <cassert>
#include <chrono>
#include <components/misc/parallelfor.hpp>
#include <stdexcept>
#include <string>

int main()
{
    Misc::parallelFor(0, 4, [](std::size_t) { std::terminate(); });
    for (unsigned workers : { 0, 1, 4 })
    {
        std::vector<unsigned> visits(1000);
        Misc::parallelFor(visits.size(), workers, [&](std::size_t i) { ++visits[i]; });
        for (auto visitsForIndex : visits)
            assert(visitsForIndex == 1);
    }
    std::atomic<unsigned> completed{ 0 };
    try
    {
        Misc::parallelFor(32, 4, [&](std::size_t i) {
            ++completed;
            if (i == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                throw std::runtime_error("first input");
            }
            if (i == 1)
                throw std::runtime_error("first completion");
        });
        assert(false);
    }
    catch (const std::runtime_error& e)
    {
        assert(std::string(e.what()) == "first input");
        assert(completed == 32);
    }
}
