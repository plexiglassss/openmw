#ifndef OPENMW_COMPONENTS_ESM_GLOBALMAP_H
#define OPENMW_COMPONENTS_ESM_GLOBALMAP_H

#include <cstdint>
#include <set>
#include <vector>

#include "components/esm/defs.hpp"
#include "components/esm/refid.hpp"

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    // format 0, saved games only

    ///< \brief An image containing the explored areas on the global map.
    struct GlobalMap
    {
        constexpr static RecNameInts sRecordId = REC_GMAP;

        // The minimum and maximum cell coordinates
        struct Bounds
        {
            int32_t mMinX, mMaxX, mMinY, mMaxY;
        };

        Bounds mBounds;

        std::vector<char> mImageData;

        typedef std::pair<int, int> CellId;
        std::set<CellId> mMarkers;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };

    // Optional save record, separate from GMAP so older engines can skip it.
    // String record identities avoid binding discoveries to content-file slots.
    struct LocationMarker
    {
        RefId mDestination;
        RefId mDoor;
        int32_t mSourceX = 0;
        int32_t mSourceY = 0;
        float mWorldX = 0;
        float mWorldY = 0;
    };

    struct LocationMarkers
    {
        constexpr static RecNameInts sRecordId = REC_LMRK;

        std::vector<LocationMarker> mMarkers;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };

}

#endif
