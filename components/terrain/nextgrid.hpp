#ifndef OPENMW_COMPONENTS_TERRAIN_NEXTGRID_H
#define OPENMW_COMPONENTS_TERRAIN_NEXTGRID_H

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Terrain
{
    // Prepare one neighbouring grid, not a corridor of runtime cells. Positions
    // are used only to choose immutable resource snapshots, never to activate.
    inline std::vector<std::pair<int, int>> nextGridCells(
        int x, int y, int radius, float dx, float dy, float cellSize)
    {
        std::vector<std::pair<int, int>> cells;
        const float speed = std::max(std::abs(dx), std::abs(dy));
        if (!std::isfinite(speed) || !std::isfinite(dx) || !std::isfinite(dy)
            || speed < 1.f || speed > cellSize || radius < 0)
            return cells;
        const int sx = std::abs(dx) >= speed * 0.35f ? (dx > 0 ? 1 : -1) : 0;
        const int sy = std::abs(dy) >= speed * 0.35f ? (dy > 0 ? 1 : -1) : 0;
        for (int xx = x + sx - radius; xx <= x + sx + radius; ++xx)
            for (int yy = y + sy - radius; yy <= y + sy + radius; ++yy)
                if (xx < x - radius || xx > x + radius || yy < y - radius || yy > y + radius)
                    cells.emplace_back(xx, yy);
        return cells;
    }
}
#endif
