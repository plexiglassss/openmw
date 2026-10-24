#ifndef COMPONENTS_MISC_TRANSIENTREFINDEX_H
#define COMPONENTS_MISC_TRANSIENTREFINDEX_H

#include <map>
#include <memory>

namespace Misc
{
    // A save-load-local index over stable list nodes. Small workloads avoid index
    // allocation; repeated searches in large lists build once. First match wins.
    template <class Key, class Value>
    class TransientRefIndex
    {
        unsigned int mSearches = 0;
        bool mIndexed = false;
        std::map<Key, Value*> mEntries;

    public:
        template <class List, class GetKey>
        Value* find(List& list, const Key& key, GetKey getKey)
        {
            if (!mIndexed && (mSearches < 8 || list.size() < 64))
            {
                if (mSearches < 8)
                    ++mSearches;
                for (auto& value : list)
                    if (getKey(value) == key)
                        return std::addressof(value);
                return nullptr;
            }
            if (!mIndexed)
            {
                for (auto& value : list)
                    mEntries.try_emplace(getKey(value), std::addressof(value));
                mIndexed = true;
            }
            const auto found = mEntries.find(key);
            return found == mEntries.end() ? nullptr : found->second;
        }

        void inserted(const Key& key, Value* value)
        {
            if (mIndexed)
                mEntries.try_emplace(key, value);
        }
    };
}

#endif
