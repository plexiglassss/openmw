#ifndef OPENMW_COMPONENTS_MISC_PARALLELFOR_H
#define OPENMW_COMPONENTS_MISC_PARALLELFOR_H

#include <algorithm>
#include <atomic>
#include <exception>
#include <thread>
#include <vector>

namespace Misc
{
    // Each index is visited exactly once. Results must be stored in independent slots.
    // Join every worker before propagating the first exception in input order.
    template <class Function>
    void parallelFor(std::size_t count, std::size_t maxWorkers, Function&& function)
    {
        if (count == 0)
            return;
        const auto workers = std::min(count, std::max(std::size_t{ 1 }, maxWorkers));
        std::vector<std::exception_ptr> errors(count);
        std::atomic_size_t next{ 0 };
        auto work = [&] {
            for (;;)
            {
                const auto index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= count)
                    return;
                try
                {
                    function(index);
                }
                catch (...)
                {
                    errors[index] = std::current_exception();
                }
            }
        };
        {
            std::vector<std::jthread> threads;
            threads.reserve(workers - 1);
            for (std::size_t i = 1; i < workers; ++i)
                threads.emplace_back(work);
            work();
        }
        for (const auto& error : errors)
            if (error)
                std::rethrow_exception(error);
    }
}
#endif
