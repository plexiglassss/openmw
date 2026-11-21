#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <components/resource/boundedcache.hpp>

namespace { std::atomic_bool failNextAllocation = false; }
void* operator new(std::size_t size)
{
    if (failNextAllocation.exchange(false))
        throw std::bad_alloc{};
    if (auto* result = std::malloc(size ? size : 1))
        return result;
    throw std::bad_alloc{};
}
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

int main()
{
    Resource::BoundedCache<int, std::shared_ptr<int>> cache(2, 100, 1.0);
    cache.put(1, std::make_shared<int>(7), 40, cache.generation(), 0.0);
    const auto before = cache.stats();
    bool threw = false;
    failNextAllocation = true;
    try { cache.get(1, 2.0); }
    catch (const std::bad_alloc&) { threw = true; }
    failNextAllocation = false;
    const auto after = cache.stats();
    std::cout << "bad_alloc=" << threw << " before_entries=" << before.mCache.mSize
              << " before_bytes=" << before.mBytes << " after_entries=" << after.mCache.mSize
              << " after_bytes=" << after.mBytes << '\n';
    // The exception must not leave a partially erased map/list/accounting state.
    return threw && after.mCache.mSize == before.mCache.mSize && after.mBytes == before.mBytes ? 0 : 1;
}
