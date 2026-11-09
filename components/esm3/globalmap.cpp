#include "globalmap.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

#include <cmath>
#include <utility>

namespace ESM
{

    void GlobalMap::load(ESMReader& esm)
    {
        esm.getHNT("BNDS", mBounds.mMinX, mBounds.mMaxX, mBounds.mMinY, mBounds.mMaxY);

        esm.getSubNameIs("DATA");
        esm.getSubHeader();
        mImageData.resize(esm.getSubSize());
        esm.getExact(mImageData.data(), mImageData.size());

        while (esm.isNextSub("MRK_"))
        {
            esm.getSubHeader();
            CellId cell;
            esm.getT(cell.first);
            esm.getT(cell.second);
            mMarkers.insert(cell);
        }
    }

    void GlobalMap::save(ESMWriter& esm) const
    {
        esm.writeHNT("BNDS", mBounds);

        esm.startSubRecord("DATA");
        esm.write(mImageData.data(), mImageData.size());
        esm.endRecord("DATA");

        for (std::set<CellId>::const_iterator it = mMarkers.begin(); it != mMarkers.end(); ++it)
        {
            esm.startSubRecord("MRK_");
            esm.writeT(it->first);
            esm.writeT(it->second);
            esm.endRecord("MRK_");
        }
    }

    void LocationMarkers::load(ESMReader& esm)
    {
        mMarkers.clear();
        std::uint32_t version = 0;
        esm.getHNT(version, "VERS");
        if (version != 1)
        {
            esm.skipRecord();
            return;
        }

        // Each entry is bounded by the record itself; never allocate from a
        // count supplied by the save file.
        while (esm.hasMoreSubs())
        {
            LocationMarker marker;
            marker.mDestination = esm.getHNRefId("NAME");
            marker.mDoor = esm.getHNRefId("DOOR");
            esm.getHNT("GRID", marker.mSourceX, marker.mSourceY);
            esm.getHNT("POS_", marker.mWorldX, marker.mWorldY);
            if (marker.mDestination.empty() || marker.mDoor.empty())
                esm.fail("Empty location marker identity");
            if (!std::isfinite(marker.mWorldX) || !std::isfinite(marker.mWorldY))
                esm.fail("Non-finite location marker position");
            mMarkers.push_back(std::move(marker));
        }
    }

    void LocationMarkers::save(ESMWriter& esm) const
    {
        esm.writeHNT("VERS", std::uint32_t{ 1 });
        for (const LocationMarker& marker : mMarkers)
        {
            esm.writeHNRefId("NAME", marker.mDestination);
            esm.writeHNRefId("DOOR", marker.mDoor);
            esm.startSubRecord("GRID");
            esm.writeT(marker.mSourceX);
            esm.writeT(marker.mSourceY);
            esm.endRecord("GRID");
            esm.startSubRecord("POS_");
            esm.writeT(marker.mWorldX);
            esm.writeT(marker.mWorldY);
            esm.endRecord("POS_");
        }
    }

}
