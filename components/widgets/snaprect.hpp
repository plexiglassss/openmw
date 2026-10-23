#ifndef OPENMW_COMPONENTS_WIDGETS_SNAPRECT_H
#define OPENMW_COMPONENTS_WIDGETS_SNAPRECT_H
#include <cmath>
#include <span>
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
}
#endif
