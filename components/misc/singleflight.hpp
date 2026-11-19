#ifndef OPENMW_COMPONENTS_MISC_SINGLEFLIGHT_H
#define OPENMW_COMPONENTS_MISC_SINGLEFLIGHT_H

#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <utility>

namespace Misc
{
    // Coalesce concurrent work only. Completed results belong in the caller's
    // cache, not here. The factory must not recursively request the same key.
    template <class Key, class Value>
    class SingleFlight
    {
        std::mutex mMutex;
        std::map<Key, std::shared_future<Value>, std::less<>> mPending;

    public:
        template <class Factory, class Wait>
        Value get(const Key& key, Factory&& factory, Wait&& wait)
        {
            std::promise<Value> promise;
            std::shared_future<Value> future;
            bool producer = false;
            {
                const std::lock_guard lock(mMutex);
                auto it = mPending.find(key);
                if (it != mPending.end())
                    future = it->second;
                else
                {
                    future = promise.get_future().share();
                    mPending.emplace(key, future);
                    producer = true;
                }
            }
            if (!producer)
                return wait(future);

            try
            {
                Value value = factory(); // No registry lock while doing work.
                const std::lock_guard lock(mMutex);
                promise.set_value(std::move(value));
                mPending.erase(key);
            }
            catch (...)
            {
                const std::lock_guard lock(mMutex);
                promise.set_exception(std::current_exception());
                mPending.erase(key); // Future requests can retry failed work.
            }
            return future.get();
        }
    };
}
#endif
