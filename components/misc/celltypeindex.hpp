#ifndef OPENMW_MISC_CELLTYPEINDEX_H
#define OPENMW_MISC_CELLTYPEINDEX_H

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Misc
{
    // A negative-only index. Candidates are a superset: superseded/deleted
    // definitions may cause extra loading, but must never hide a live result.
    // Publish only after the entire source scan succeeds. Unknown cells, unknown
    // base types, and a changed base-record type map always use normal loading.
    template <class CellKey>
    class CellTypeIndex
    {
        struct Candidates
        {
            bool unknown = false;
            std::vector<std::uint32_t> types;
        };
        std::unordered_map<CellKey, Candidates> mCells;
        bool mComplete = false;
        bool mTypesCompatible = true;

    public:
        void addCell(CellKey cell) { mCells.try_emplace(cell); }
        void addCandidate(CellKey cell, std::uint32_t type)
        {
            auto& entry = mCells[cell];
            if (type == 0)
                entry.unknown = true;
            else if (std::find(entry.types.begin(), entry.types.end(), type) == entry.types.end())
                entry.types.push_back(type);
        }
        void complete() { mComplete = true; }
        void resetTypeCompatibility() { mTypesCompatible = true; }
        void observeTypeChange(std::uint32_t originalType, std::uint32_t currentType)
        {
            // Originally unknown base IDs already make their cells unknown.
            // New generated IDs cannot invalidate any known negative result.
            if (originalType != 0 && originalType != currentType)
                mTypesCompatible = false;
        }
        bool mayContain(CellKey cell, std::uint32_t type) const
        {
            if (!mComplete || !mTypesCompatible || type == 0)
                return true;
            const auto found = mCells.find(cell);
            if (found == mCells.end() || found->second.unknown)
                return true;
            const auto& types = found->second.types;
            return std::find(types.begin(), types.end(), type) != types.end();
        }
    };
}

#endif
