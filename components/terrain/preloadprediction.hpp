#ifndef OPENMW_COMPONENTS_TERRAIN_PRELOADPREDICTION_H
#define OPENMW_COMPONENTS_TERRAIN_PRELOADPREDICTION_H

#include <algorithm>
#include <cmath>

namespace Terrain
{
    struct PreloadPrediction
    {
        float mX = 0;
        float mY = 0;
        double mInterval = 0.25;
    };

    // Limit the projected distance, not the speed. A fast continuous movement
    // still prepares the next grid; a real discontinuity never sweeps a corridor.
    inline PreloadPrediction predictPreload(float dx, float dy, double dt, double lead, float cellSize)
    {
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dt) || !std::isfinite(lead)
            || !std::isfinite(cellSize) || dt <= 1e-6 || dt > 0.5 || lead < 0 || cellSize <= 0)
            return {};
        const double distance = std::max(std::abs(dx), std::abs(dy));
        if (distance == 0 || distance > cellSize)
            return {};
        const double speed = distance / dt;
        const double scale = std::min(std::min(lead, 4.0) / dt, double(cellSize) / distance);
        return { static_cast<float>(dx * scale), static_cast<float>(dy * scale),
            std::clamp(cellSize / (speed * 8.0), 0.025, 0.25) };
    }
}

#endif
