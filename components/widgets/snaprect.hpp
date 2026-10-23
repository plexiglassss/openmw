#ifndef OPENMW_COMPONENTS_WIDGETS_SNAPRECT_H
#define OPENMW_COMPONENTS_WIDGETS_SNAPRECT_H
#include <cmath>
#include <span>
#include <vector>
namespace Gui
{
    struct SnapRect
    {
        int left, top, width, height;
        int right() const { return left + width; }
        int bottom() const { return top + height; }
    };
    inline SnapRect snapRect(
        SnapRect p, std::span<const SnapRect> others, int screenWidth, int screenHeight, int distance)
    {
        int dx = distance + 1, dy = distance + 1;
        auto choose = [](int candidate, int& best) {
            if (std::abs(candidate) < std::abs(best))
                best = candidate;
        };
        choose(-p.left, dx);
        choose(screenWidth - p.right(), dx);
        choose(-p.top, dy);
        choose(screenHeight - p.bottom(), dy);
        for (const auto& r : others)
        {
            if (p.top <= r.bottom() + distance && p.bottom() >= r.top - distance)
                for (int candidate : { r.left - p.left, r.right() - p.right(), r.left - p.right(), r.right() - p.left })
                    choose(candidate, dx);
            if (p.left <= r.right() + distance && p.right() >= r.left - distance)
                for (int candidate : { r.top - p.top, r.bottom() - p.bottom(), r.top - p.bottom(), r.bottom() - p.top })
                    choose(candidate, dy);
        }
        if (std::abs(dx) <= distance)
            p.left += dx;
        if (std::abs(dy) <= distance)
            p.top += dy;
        return p;
    }
    // Edge bits: left=1, right=2, top=4, bottom=8. Opposite edges stay fixed.
    inline SnapRect snapResizeRect(SnapRect p, std::span<const SnapRect> others,
        int screenWidth, int screenHeight, int distance, int edges, int minWidth, int minHeight)
    {
        const int right = p.right(), bottom = p.bottom();
        auto snap = [distance](int value, const auto& candidates, int lo, int hi) {
            int result = value, best = distance + 1;
            for (int candidate : candidates)
                if (candidate >= lo && candidate <= hi && std::abs(candidate - value) < best)
                { result = candidate; best = std::abs(candidate - value); }
            return result;
        };
        std::vector<int> xs{0, screenWidth}, ys{0, screenHeight};
        for (const auto& r : others)
        {
            if (p.top <= r.bottom() + distance && bottom >= r.top - distance)
            { xs.push_back(r.left); xs.push_back(r.right()); }
            if (p.left <= r.right() + distance && right >= r.left - distance)
            { ys.push_back(r.top); ys.push_back(r.bottom()); }
        }
        if (edges & 1) { p.left = snap(p.left, xs, 0, right - minWidth); p.width = right - p.left; }
        else if (edges & 2) p.width = snap(right, xs, p.left + minWidth, screenWidth) - p.left;
        if (edges & 4) { p.top = snap(p.top, ys, 0, bottom - minHeight); p.height = bottom - p.top; }
        else if (edges & 8) p.height = snap(bottom, ys, p.top + minHeight, screenHeight) - p.top;
        return p;
    }

}
#endif
