#ifndef OPENMW_COMPONENTS_TERRAIN_SCENERYCOVERAGE_H
#define OPENMW_COMPONENTS_TERRAIN_SCENERYCOVERAGE_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace Terrain
{
    using SceneryCell = std::pair<int, int>;

    struct SceneryCoverage
    {
        std::vector<std::size_t> mRequests;
        std::set<SceneryCell> mCells;
        bool mCanReplace = true;
    };

    // The selected terrain leaves partition space, but several subcell leaves
    // can own one scenery cell. They must all be ready before replacing its
    // bridge. A large optimized batch keeps its original merge boundary.
    template <class Request, class Grid>
    std::vector<SceneryCoverage> sceneryCoverage(std::span<const Request> requests, const Grid& grid)
    {
        std::vector<SceneryCoverage> groups;
        std::map<SceneryCell, std::size_t> owners;
        for (std::size_t i = 0; i < requests.size(); ++i)
        {
            const auto& request = requests[i];
            if (request.mActiveGrid)
                continue;
            const int x0 = static_cast<int>(std::floor(request.mCenter.x() - request.mSize / 2));
            const int y0 = static_cast<int>(std::floor(request.mCenter.y() - request.mSize / 2));
            const int x1 = static_cast<int>(std::ceil(request.mCenter.x() + request.mSize / 2));
            const int y1 = static_cast<int>(std::ceil(request.mCenter.y() + request.mSize / 2));
            SceneryCoverage group;
            group.mRequests.push_back(i);
            std::set<std::size_t> merge;
            for (int x = x0; x < x1; ++x)
                for (int y = y0; y < y1; ++y)
                {
                    if (x >= grid.x() && y >= grid.y() && x < grid.z() && y < grid.w())
                    {
                        // Never draw a retained merged batch across newly active
                        // ownership. Reference positions may extend beyond cells.
                        group.mCanReplace = false;
                        continue;
                    }
                    const SceneryCell cell{ x, y };
                    group.mCells.insert(cell);
                    if (auto found = owners.find(cell); found != owners.end())
                        merge.insert(found->second);
                }
            if (group.mCells.empty())
                continue;
            for (const auto index : merge)
            {
                auto& old = groups[index];
                for (const auto previous : old.mRequests)
                {
                    const auto& other = requests[previous];
                    const float half = (request.mSize + other.mSize) / 2;
                    if (std::abs(request.mCenter.x() - other.mCenter.x()) < half
                        && std::abs(request.mCenter.y() - other.mCenter.y()) < half)
                        group.mCanReplace = false;
                }
                group.mRequests.insert(group.mRequests.end(), old.mRequests.begin(), old.mRequests.end());
                group.mCells.insert(old.mCells.begin(), old.mCells.end());
                group.mCanReplace &= old.mCanReplace;
                old = {};
            }
            const auto index = groups.size();
            for (const auto& cell : group.mCells)
                owners[cell] = index;
            groups.push_back(std::move(group));
        }
        std::erase_if(groups, [](const auto& group) { return group.mCells.empty(); });
        return groups;
    }

    template <class Ready>
    bool sceneryReplacementReady(const SceneryCoverage& group, Ready&& ready)
    {
        return group.mCanReplace
            && std::all_of(group.mRequests.begin(), group.mRequests.end(), std::forward<Ready>(ready));
    }
}
#endif
