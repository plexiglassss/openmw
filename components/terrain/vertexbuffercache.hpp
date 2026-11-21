#ifndef OPENMW_COMPONENTS_TERRAIN_VERTEXBUFFERCACHE_H
#define OPENMW_COMPONENTS_TERRAIN_VERTEXBUFFERCACHE_H

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <span>
#include <tuple>

#include <osg/Vec2f>

#include <components/esm/refid.hpp>
#include <components/misc/singleflight.hpp>
#include <components/resource/objectcache.hpp>

#include "vertexbuffer.hpp"

namespace Misc { class NativeJobPool; }

namespace Terrain
{
    class Storage;

    struct VertexBufferKey
    {
        osg::Vec2f mCenter;
        float mSize;
        unsigned char mLod;

        friend bool operator<(const VertexBufferKey& a, const VertexBufferKey& b)
        {
            return std::tie(a.mCenter, a.mSize, a.mLod) < std::tie(b.mCenter, b.mSize, b.mLod);
        }
    };

    class VertexBufferCache
    {
    public:
        using Value = std::shared_ptr<const VertexBuffer>;
        VertexBufferCache(Storage& storage, ESM::RefId worldspace);
        Value get(const VertexBufferKey& key);
        // Joins all borrowed inputs before returning. A failed speculative fill is
        // retried by the real request, preserving the existing failure policy.
        void prepare(std::span<const VertexBufferKey> keys, Misc::NativeJobPool& jobs, const std::atomic_bool& abort);
        void clear();
        void update(double referenceTime, double expiryDelay);
        void releaseGLObjects(osg::State* state);
        Resource::CacheStats getStats() const { return mCache.getStats(); }

    private:
        Storage& mStorage;
        ESM::RefId mWorldspace;
        std::shared_mutex mBuildMutex;
        Resource::GenericObjectCache<VertexBufferKey, Value> mCache;
        Misc::SingleFlight<VertexBufferKey, Value> mPending;
    };
}

#endif
