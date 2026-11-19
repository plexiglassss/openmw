#ifndef COMPONENTS_MISC_TRANSIENTREFINDEX_H
#define COMPONENTS_MISC_TRANSIENTREFINDEX_H

#include <map>
#include <memory>

namespace Misc
{
    // A load-local index over stable list nodes. Small workloads avoid index
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
                // Build transactionally: a caller may catch an allocation/key
                // exception, erase list nodes, and retry with this index.
                decltype(mEntries) entries;
                for (auto& value : list)
                    entries.try_emplace(getKey(value), std::addressof(value));
                mEntries.swap(entries);
                mIndexed = true;
            }
            const auto found = mEntries.find(key);
            return found == mEntries.end() ? nullptr : found->second;
        }

        void inserted(const Key& key, Value* value)
        {
            if (mIndexed)
            {
                try
                {
                    mEntries.try_emplace(key, value);
                }
                catch (...)
                {
                    // The list append already succeeded. Forget the index so a
                    // caught exception cannot hide that node on the next lookup.
                    mEntries.clear();
                    mIndexed = false;
                    mSearches = 0;
                    throw;
                }
            }
        }

        // Call when all list nodes for this key are about to be erased. Other
        // std::list node addresses and first-match precedence stay unchanged.
        void removed(const Key& key)
        {
            if (mIndexed)
                mEntries.erase(key);
        }
    };
}

#endif
