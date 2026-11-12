#ifndef OPENMW_COMPONENTS_MAPMARKERS_ICONS_HPP
#define OPENMW_COMPONENTS_MAPMARKERS_ICONS_HPP

#include "classification.hpp"

#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace MapMarkers
{
    // A skin contains full 32x32 marker images. The first texture retains its
    // artwork's own colours. The optional second texture supplies the type palette.
    struct IconTextures
    {
        std::string vanilla;
        std::string byType;
        const std::string& select(bool useTypeColours) const { return useTypeColours && !byType.empty() ? byType : vanilla; }
    };

    class IconSet
    {
    public:
        using Exists = std::function<bool(std::string_view)>;
        static IconSet read(std::istream& input, const Exists& exists);
        const IconTextures& world(Category category) const;
        std::size_t classifyDoor(std::string_view destination, std::string_view doorId, bool exteriorLike) const;
        const IconTextures& door(std::size_t index) const;

    private:
        struct Rule { std::string text; std::size_t icon; };
        std::array<IconTextures, categoryCount> mWorld;
        std::vector<IconTextures> mDoors{ IconTextures{} }; // Index zero uses the original door marker.
        std::size_t mDefaultDoor = 0;
        std::vector<Rule> mNames, mDoorIds;
    };

    // Sizes are in logical GUI pixels, after map zoom and cluster enlargement.
    float locationIconExtent(float base, float zoom, std::size_t weight, bool entrance, int maximum);
    int localIconExtent(int base, float zoom, int maximum);
}
#endif
