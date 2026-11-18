#ifndef OPENMW_COMPONENTS_RESOURCE_COLLISIONINSTANCECACHE_H
#define OPENMW_COMPONENTS_RESOURCE_COLLISIONINSTANCECACHE_H

#include <cmath>
#include <map>
#include <mutex>
#include <vector>

#include "bulletshape.hpp"
#include "cachestats.hpp"
#include <components/vfs/pathutil.hpp>

namespace Resource
{
    // Detached instances are single-use: taking one removes it from the cache.
    // Scale is exact, not quantized. Different runtime scales safely miss.
    class CollisionInstanceCache : public osg::Referenced
    {
        struct KeyView
        {
            VFS::Path::NormalizedView mName;
            float mScale;
        };
        struct Key
        {
            VFS::Path::Normalized mName;
            float mScale;
            operator KeyView() const { return { mName, mScale }; }
        };
        struct Compare
        {
            using is_transparent = void;
            bool operator()(KeyView a, KeyView b) const
            {
                if (a.mName != b.mName)
                    return a.mName < b.mName;
                return a.mScale < b.mScale;
            }
        };
        using Entries = std::multimap<Key, osg::ref_ptr<BulletShapeInstance>, Compare>;
        Entries mEntries;
        mutable std::mutex mMutex;
        std::size_t mGet = 0;
        std::size_t mHit = 0;
        std::size_t mExpired = 0;

    public:
        void add(VFS::Path::NormalizedView name, float scale, osg::ref_ptr<BulletShapeInstance> instance)
        {
            if (!instance || !std::isfinite(scale))
                return;
            std::lock_guard lock(mMutex);
            mEntries.emplace(Key{ VFS::Path::Normalized(name), scale }, std::move(instance));
        }

        osg::ref_ptr<BulletShapeInstance> take(VFS::Path::NormalizedView name, float scale)
        {
            std::lock_guard lock(mMutex);
            ++mGet;
            if (!std::isfinite(scale))
                return {};
            const auto it = mEntries.find(KeyView{ name, scale });
            if (it == mEntries.end())
                return {};
            auto instance = std::move(it->second);
            mEntries.erase(it);
            ++mHit;
            return instance;
        }

        void removeUnreferencedObjectsInCache()
        {
            std::vector<osg::ref_ptr<BulletShapeInstance>> retired;
            {
                std::lock_guard lock(mMutex);
                for (auto it = mEntries.begin(); it != mEntries.end();)
                {
                    if (it->second->referenceCount() <= 1)
                    {
                        retired.push_back(std::move(it->second));
                        it = mEntries.erase(it);
                        ++mExpired;
                    }
                    else
                        ++it;
                }
            }
            // Destruction stays outside the lock, on the existing cache worker.
        }

        void clear()
        {
            Entries retired;
            {
                std::lock_guard lock(mMutex);
                retired.swap(mEntries);
            }
        }

        CacheStats getStats() const
        {
            std::lock_guard lock(mMutex);
            return { .mSize = mEntries.size(), .mGet = mGet, .mHit = mHit, .mExpired = mExpired };
        }
    };
}

#endif
