#ifndef OPENMW_COMPONENTS_MAPMARKERS_CLASSIFICATION_HPP
#define OPENMW_COMPONENTS_MAPMARKERS_CLASSIFICATION_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace MapMarkers
{
    // Values define persisted category-setting bits. Append; do not reorder.
    enum class Category : std::uint8_t
    {
        City,
        Town,
        Settlement,
        House,
        Cave,
        Mine,
        Tomb,
        Barrow,
        Dwemer,
        Daedric,
        Stronghold,
        Fort,
        Tower,
        Ruin,
        Camp,
        Ship,
        Shipwreck,
        Landmark,
        Misc,
    };

    inline constexpr std::size_t categoryCount = 19;

    constexpr bool isValid(Category category)
    {
        return static_cast<std::size_t>(category) < categoryCount;
    }

    constexpr std::uint32_t bit(Category category)
    {
        return isValid(category) ? std::uint32_t{ 1 } << static_cast<unsigned>(category) : 0;
    }

    // Ordinary settlement doors would flood the world map with house markers.
    constexpr bool meaningfulEntrance(Category category)
    {
        return isValid(category) && category != Category::City && category != Category::Town
            && category != Category::Settlement && category != Category::House && category != Category::Misc;
    }

    struct Definition
    {
        std::string_view id;
        std::string_view label;
        std::string_view texturePath;
        std::array<float, 3> color;
    };

    // Invalid values safely use the generic marker definition.
    const Definition& definition(Category category) noexcept;

    // Position-free classification for discovery time, not the frame loop.
    // Exact canonical IDs survive translated display names. Unknown entrances
    // use their own name and distinctive model prefixes before parent names.
    // placeLabel ignores door evidence and preserves exterior place semantics.
    Category classify(std::string_view cellId, std::string_view displayName = {}, std::string_view doorModel = {},
        bool placeLabel = false);
}

#endif
