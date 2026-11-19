#ifndef OPENMW_COMPONENTS_MISC_ACTORNEIGHBOURS_HPP
#define OPENMW_COMPONENTS_MISC_ACTORNEIGHBOURS_HPP

#include "nativejobpool.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <span>
#include <vector>

namespace Misc
{
    // Broad phase only. The mechanics thread still processes every possible neighbour
    // in actor order, reads current movement, and performs the original exact tests.
    class ActorNeighbours
    {
    public:
        using Position = std::array<float, 2>;

        ActorNeighbours(std::span<const Position> positions, float radius, NativeJobPool& pool)
            : mRows(positions.size())
        {
            if (std::any_of(positions.begin(), positions.end(), [](const Position& p) {
                    return !std::isfinite(p[0]) || !std::isfinite(p[1]);
                }))
                return;

            std::vector<std::size_t> order(positions.size());
            std::iota(order.begin(), order.end(), std::size_t{ 0 });
            std::sort(order.begin(), order.end(), [&](auto a, auto b) { return positions[a][0] < positions[b][0]; });

            pool.parallelFor(positions.size(), 48, [&](std::size_t i) {
                const Position centre = positions[i];
                auto first = std::partition_point(order.begin(), order.end(), [&](std::size_t j) {
                    return centre[0] - positions[j][0] > radius;
                });
                auto& row = mRows[i];
                for (; first != order.end(); ++first)
                {
                    const auto j = *first;
                    if (positions[j][0] - centre[0] > radius)
                        break;
                    if (std::abs(positions[j][1] - centre[1]) <= radius)
                    {
                        // Crowded groups use the original loop. Bound memory even if
                        // every actor in a mod occupies the same point.
                        if (row.mIndices.size() >= 64)
                            return;
                        row.mIndices.push_back(j);
                    }
                }
                std::sort(row.mIndices.begin(), row.mIndices.end());
                row.mComplete = true;
            });
        }

        struct Row
        {
            std::vector<std::size_t> mIndices;
            bool mComplete = false;
        };
        const Row& operator[](std::size_t index) const { return mRows[index]; }

    private:
        std::vector<Row> mRows;
    };
}

#endif
