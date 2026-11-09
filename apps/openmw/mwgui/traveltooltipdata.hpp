#ifndef OPENMW_MWGUI_TRAVELTOOLTIPDATA_HPP
#define OPENMW_MWGUI_TRAVELTOOLTIPDATA_HPP

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace MWGui::TravelTooltips
{
    // A VFS data file supplies the illustrated destination catalogue. The engine does not
    // embed third-party artwork or descriptions and never executes a mod's MWSE scripts.
    inline constexpr std::string_view manifestPath = "travel_tooltips/data.tsv";

    struct Image
    {
        int mPack = 0;
        std::string mPrefix;
        std::string mPath;
        int mWidth = 0;
        int mHeight = 0;
    };

    struct Description
    {
        std::string mMatch;
        std::string mText;
    };

    struct Data
    {
        std::vector<Image> mImages;
        std::vector<Image> mOverviews;
        std::vector<Description> mDescriptions;
        std::vector<Description> mGondoliers;
    };

    // Tab-separated fields, with \\, \t, \r and \n escaped with a backslash.
    // First record: version<TAB>1. Image records: image, pack, prefix, path, width, height.
    // Overview records: overview, index, path, width, height. Description records:
    // description (prefix) or gondolier (substring), match, text. Throws on invalid input.
    Data readData(std::istream& stream);

    using ImageAvailable = std::function<bool(std::string_view)>;

    // Longest matching prefix wins. Variants have stable order and selection for a seed.
    // Optional fallback uses pack 0, matching the original mod's fallback policy.
    const Image* destinationImage(const Data& data, std::string_view destination, int pack, bool fallback,
        std::uint64_t seed, const ImageAvailable& available);
    const Image* overviewImage(const Data& data, int index, const ImageAvailable& available);
    std::string_view destinationDescription(const Data& data, std::string_view destination, bool gondolier);
    bool hasVvardenfellOverview(std::string_view providerId);
}

#endif
