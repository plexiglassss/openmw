#ifndef OPENMW_COMPONENTS_MISC_SPATIALINDEX_HPP
#define OPENMW_COMPONENTS_MISC_SPATIALINDEX_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

namespace Misc
{
    // Performance heuristic only: false retains the exact full scan. Sampling
    // avoids allocating and sorting an index for repeated whole-list queries.
    template <class PositionAt>
    bool spatialQueryIsSelective(std::size_t size, PositionAt at,
        const std::array<float, 2>& centre, double radius)
    {
        const auto samples = std::min(size, std::size_t{ 32 });
        if (!samples)
            return false;
        std::size_t inside = 0;
        for (std::size_t i = 0; i < samples; ++i)
        {
            const auto p = at(i * size / samples);
            inside += std::abs(double(p[0]) - centre[0]) <= radius
                && std::abs(double(p[1]) - centre[1]) <= radius;
        }
        return inside < samples * 3 / 4;
    }

    // A callback (for example an attack dialogue result script) may mutate the
    // world. Continue the ORIGINAL remaining list if that happens, without
    // repeating callbacks already made or missing newly relevant later entries.
    // The visitor must return false if it erases list storage during a callback.
    template <class Iterator, class End, class Changed, class Visit>
    void visitSpatialCandidates(End end, std::span<const Iterator> candidates,
        Changed changed, Visit visit)
    {
        for (const auto current : candidates)
        {
            if (!visit(*current))
                return;
            if (changed())
            {
                for (auto remaining = std::next(current); remaining != end(); ++remaining)
                    if (!visit(*remaining))
                        return;
                return;
            }
        }
    }

    // Immutable broad phase. A query returns original indices in original order;
    // consumers retain their exact distance checks, duplicates and tie ordering.
    // No live world pointers are accessed, including when queried on workers.
    class SpatialIndex
    {
    public:
        using Position = std::array<float, 2>;

        explicit SpatialIndex(std::span<const Position> positions)
            : mPositions(positions.begin(), positions.end())
            , mOrder(positions.size())
        {
            if (positions.empty())
                return;
            Position low = positions.front(), high = low;
            for (const auto& p : positions)
                for (std::size_t axis = 0; axis < 2; ++axis)
                {
                    if (!std::isfinite(p[axis]))
                        return; // The unchanged scan defines non-finite behaviour.
                    low[axis] = std::min(low[axis], p[axis]);
                    high[axis] = std::max(high[axis], p[axis]);
                }
            mLow = low;
            const auto side = std::clamp(static_cast<std::size_t>(std::sqrt(positions.size() / 8.0)),
                std::size_t{ 1 }, std::size_t{ 128 });
            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                mDimensions[axis] = high[axis] == low[axis] ? 1 : side;
                mScale[axis] = high[axis] == low[axis] ? 0
                    : double(mDimensions[axis]) / (double(high[axis]) - low[axis]);
            }
            mOffsets.resize(mDimensions[0] * mDimensions[1] + 1);
            std::vector<std::size_t> cells;
            cells.reserve(positions.size());
            for (const auto& p : positions)
            {
                const auto cell = coordinate(p[0], 0) + coordinate(p[1], 1) * mDimensions[0];
                cells.push_back(cell);
                ++mOffsets[cell + 1];
            }
            std::partial_sum(mOffsets.begin(), mOffsets.end(), mOffsets.begin());
            auto next = mOffsets;
            for (std::size_t i = 0; i < cells.size(); ++i)
                mOrder[next[cells[i]]++] = i;
            mValid = true;
        }

        // nullopt requests the original scan. Dense queries avoid sorting a large
        // candidate list; unusual coordinates and unbounded queries also fall back.
        std::optional<std::vector<std::size_t>> query(const Position& centre, double radius) const
        {
            if (!mValid || !std::isfinite(radius) || radius < 0
                || !std::isfinite(centre[0]) || !std::isfinite(centre[1]))
                return std::nullopt;

            // Exact consumers use float subtraction, squared lengths and sqrt.
            // Expand for their rounding and for squared values flushed to zero.
            const double margin = radius * (8 * std::numeric_limits<float>::epsilon())
                + std::sqrt(double(std::numeric_limits<float>::min()));
            const double bound = radius + margin;
            std::array<std::size_t, 2> low{}, high{};
            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                low[axis] = coordinate(std::nextafter(double(centre[axis]) - bound,
                    -std::numeric_limits<double>::infinity()), axis);
                high[axis] = coordinate(std::nextafter(double(centre[axis]) + bound,
                    std::numeric_limits<double>::infinity()), axis);
            }
            std::size_t count = 0;
            for (auto y = low[1]; y <= high[1]; ++y)
                for (auto x = low[0]; x <= high[0]; ++x)
                {
                    const auto cell = x + y * mDimensions[0];
                    count += mOffsets[cell + 1] - mOffsets[cell];
                }
            if (count >= mPositions.size() * 3 / 4)
                return std::nullopt;
            std::vector<std::size_t> result;
            result.reserve(count);
            for (auto y = low[1]; y <= high[1]; ++y)
                for (auto x = low[0]; x <= high[0]; ++x)
                {
                    const auto cell = x + y * mDimensions[0];
                    for (auto i = mOffsets[cell]; i < mOffsets[cell + 1]; ++i)
                    {
                        const auto index = mOrder[i];
                        const auto& p = mPositions[index];
                        if (std::abs(double(p[0]) - centre[0]) <= bound
                            && std::abs(double(p[1]) - centre[1]) <= bound)
                            result.push_back(index);
                    }
                }
            std::sort(result.begin(), result.end());
            return result;
        }

    private:
        std::size_t coordinate(double value, std::size_t axis) const
        {
            const double scaled = (value - mLow[axis]) * mScale[axis];
            if (mScale[axis] == 0 || scaled <= 0)
                return 0;
            if (scaled >= double(mDimensions[axis]))
                return mDimensions[axis] - 1;
            return static_cast<std::size_t>(scaled);
        }

        Position mLow{};
        std::array<double, 2> mScale{};
        std::array<std::size_t, 2> mDimensions{ 1, 1 };
        std::vector<std::size_t> mOffsets;
        std::vector<Position> mPositions;
        std::vector<std::size_t> mOrder;
        bool mValid = false;
    };
}

#endif
