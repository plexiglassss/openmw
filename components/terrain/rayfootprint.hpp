#ifndef OPENMW_COMPONENTS_TERRAIN_RAYFOOTPRINT_H
#define OPENMW_COMPONENTS_TERRAIN_RAYFOOTPRINT_H

#include <algorithm>
#include <cmath>
#include <limits>

#include <osg/Vec3d>

namespace Terrain
{
    // Test only the horizontal extent. No assumption about terrain height, rotation,
    // the eye position, or the length of the calling script's ray is needed.
    struct RayFootprint
    {
        osg::Vec3d mStart;
        osg::Vec3d mEnd;

        bool valid() const
        {
            for (unsigned axis = 0; axis < 3; ++axis)
                if (!std::isfinite(mStart[axis]) || !std::isfinite(mEnd[axis]))
                    return false;
            return true;
        }

        bool intersects(double x, double y, double halfWidth) const
        {
            double enter = 0, leave = 1;
            for (unsigned axis = 0; axis != 2; ++axis)
            {
                const double center = axis == 0 ? x : y;
                const double start = mStart[axis], end = mEnd[axis];
                if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(center)
                    || !std::isfinite(halfWidth) || halfWidth < 0)
                    return true;
                // Placements and merged vertices use float transforms. Round outwards.
                const double margin = 32 * std::numeric_limits<float>::epsilon()
                    * (std::abs(center) + halfWidth + std::abs(start) + std::abs(end) + 1);
                const double low = center - halfWidth - margin, high = center + halfWidth + margin;
                if (start == end)
                {
                    if (start < low || start > high)
                        return false;
                    continue;
                }
                const double a = (low - start) / (end - start), b = (high - start) / (end - start);
                enter = std::max(enter, std::min(a, b));
                leave = std::min(leave, std::max(a, b));
                if (enter > leave)
                    return false;
            }
            return true;
        }
    };
}

#endif
