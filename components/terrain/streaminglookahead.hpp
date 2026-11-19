#ifndef OPENMW_COMPONENTS_TERRAIN_STREAMINGLOOKAHEAD_H
#define OPENMW_COMPONENTS_TERRAIN_STREAMINGLOOKAHEAD_H

#include <algorithm>
#include <cmath>
#include <optional>

#include <osg/Vec3f>
#include <osg/Vec4i>

namespace Terrain
{
    // Per-camera motion state. Planning does not load, activate or publish cells.
    class StreamingLookAhead
    {
    public:
        struct Plan
        {
            osg::Vec3f mCurrent;
            osg::Vec3f mPredicted;
            osg::Vec4i mPredictedGrid;
            float mMargin;
        };

        std::optional<Plan> update(const osg::Vec3f& point, const osg::Vec4i& grid, double now, float cellSize)
        {
            if (!(cellSize > 0) || !std::isfinite(cellSize) || !std::isfinite(now) || !std::isfinite(point.x())
                || !std::isfinite(point.y()) || !std::isfinite(point.z()))
                return {};
            const double dt = now - mLastTime;
            const auto moved = point - mLastPoint;
            const bool reset = mLastTime < 0 || now < mLastTime || moved.length2() > cellSize * cellSize;
            if (!reset && dt > 0 && dt <= 0.25)
            {
                const float blend = 1.f - std::exp(-static_cast<float>(dt) / 0.2f);
                mVelocity = mVelocity * (1.f - blend) + moved * (blend / static_cast<float>(dt));
            }
            else if (reset || dt != 0)
                mVelocity.set(0, 0, 0);
            mLastPoint = point;
            mLastTime = now;
            if (!reset && now < mNextPlan)
                return {};
            mNextPlan = now + 0.1; // at most ten planning passes per second

            auto offset = mVelocity * 2.f;
            const float length = offset.length();
            if (length > cellSize)
                offset *= cellSize / length;
            const auto predicted = point + offset;
            const int dx = static_cast<int>(std::floor(predicted.x() / cellSize) - std::floor(point.x() / cellSize));
            const int dy = static_cast<int>(std::floor(predicted.y() / cellSize) - std::floor(point.y() / cellSize));
            const osg::Vec4i predictedGrid(grid.x() + dx, grid.y() + dy, grid.z() + dx, grid.w() + dy);
            return Plan{ point, predicted, predictedGrid, cellSize };
        }

    private:
        osg::Vec3f mLastPoint;
        osg::Vec3f mVelocity;
        double mLastTime = -1;
        double mNextPlan = 0;
    };
}
#endif
