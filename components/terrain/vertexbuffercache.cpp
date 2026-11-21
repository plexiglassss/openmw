#include "vertexbuffercache.hpp"

#include <cmath>
#include <stdexcept>

#include <components/diagnostics/timing.hpp>
#include <components/misc/nativejobpool.hpp>

#include "storage.hpp"

namespace Terrain
{
    VertexBufferCache::VertexBufferCache(Storage& storage, ESM::RefId worldspace)
        : mStorage(storage), mWorldspace(worldspace)
    {
    }

    VertexBufferCache::Value VertexBufferCache::get(const VertexBufferKey& key)
    {
        if (!(key.mSize > 0) || !std::isfinite(key.mSize)
            || !std::isfinite(key.mCenter.x()) || !std::isfinite(key.mCenter.y()))
            throw std::invalid_argument("Invalid terrain vertex buffer key");
        const std::shared_lock buildLock(mBuildMutex);
        if (auto value = mCache.getRefFromObjectCache(key))
            return value;
        return mPending.get(key, [&]() -> Value {
            // A prior producer may have finished between lookup and admission.
            if (auto value = mCache.getRefFromObjectCache(key))
                return value;
            Diagnostics::Scope timing("terrain", "ground", "fillVertices");
            auto value = std::make_shared<VertexBuffer>();
            mStorage.fillVertexBuffers(key.mLod, key.mSize, key.mCenter, mWorldspace,
                *value->mPositions, *value->mNormals, *value->mColours);
            value->seal();
            mCache.addEntryToObjectCache(key, value);
            return value;
        }, [](const auto& future) { return future.get(); });
    }

    void VertexBufferCache::prepare(
        std::span<const VertexBufferKey> keys, Misc::NativeJobPool& jobs, const std::atomic_bool& abort)
    {
        jobs.parallelFor(keys.size(), 1, [&](std::size_t i) {
            if (abort.load(std::memory_order_relaxed))
                return;
            try { get(keys[i]); }
            catch (const std::exception&) { } // Demand loading retries on the owner.
        });
    }

    void VertexBufferCache::clear()
    {
        // An old fill cannot repopulate the cache after an invalidation completes.
        const std::unique_lock buildLock(mBuildMutex);
        mCache.clear();
    }

    void VertexBufferCache::update(double referenceTime, double expiryDelay)
    {
        mCache.update(referenceTime, expiryDelay);
    }

    void VertexBufferCache::releaseGLObjects(osg::State* state)
    {
        const std::unique_lock buildLock(mBuildMutex);
        mCache.releaseGLObjects(state);
    }
}
