#ifndef OPENMW_COMPONENTS_SCENEUTIL_CELLOBJECTINDEX_H
#define OPENMW_COMPONENTS_SCENEUTIL_CELLOBJECTINDEX_H

#include <map>
#include <set>

namespace SceneUtil
{
    // Non-owning membership, used only on the scene thread. Object identities
    // survive movement between cells; replacing membership cannot duplicate one.
    template <class Object, class Cell>
    class CellObjectIndex
    {
    public:
        void set(Object object, Cell cell)
        {
            const auto found = mCells.find(object);
            if (found != mCells.end() && found->second == cell)
                return;
            erase(object);
            mObjects[cell].insert(object);
            mCells.emplace(object, cell);
        }

        void erase(Object object)
        {
            const auto found = mCells.find(object);
            if (found == mCells.end())
                return;
            auto group = mObjects.find(found->second);
            group->second.erase(object);
            if (group->second.empty())
                mObjects.erase(group);
            mCells.erase(found);
        }

        std::set<Object> take(Cell cell)
        {
            const auto found = mObjects.find(cell);
            if (found == mObjects.end())
                return {};
            auto objects = std::move(found->second);
            mObjects.erase(found);
            for (const auto object : objects)
                mCells.erase(object);
            return objects;
        }

        void clear()
        {
            mCells.clear();
            mObjects.clear();
        }
        std::size_t size() const { return mCells.size(); }

    private:
        std::map<Object, Cell> mCells;
        std::map<Cell, std::set<Object>> mObjects;
    };
}

#endif
