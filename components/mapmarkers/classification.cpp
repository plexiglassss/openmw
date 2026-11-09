#include "classification.hpp"

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string>

namespace MapMarkers
{
    namespace
    {
        constexpr std::array<Definition, categoryCount> sDefinitions{ {
            { "city", "City", "textures/mapmarkers/city.png", { 0.96f, 0.80f, 0.38f } },
            { "town", "Town", "textures/mapmarkers/town.png", { 0.94f, 0.84f, 0.52f } },
            { "settlement", "Settlement", "textures/mapmarkers/settlement.png", { 0.91f, 0.82f, 0.59f } },
            { "house", "House", "textures/mapmarkers/house.png", { 0.91f, 0.80f, 0.64f } },
            { "cave", "Cave", "textures/mapmarkers/cave.png", { 0.78f, 0.80f, 0.83f } },
            { "mine", "Mine", "textures/mapmarkers/mine.png", { 0.91f, 0.69f, 0.40f } },
            { "tomb", "Tomb", "textures/mapmarkers/tomb.png", { 0.72f, 0.76f, 0.88f } },
            { "barrow", "Barrow", "textures/mapmarkers/barrow.png", { 0.70f, 0.83f, 0.75f } },
            { "dwemer", "Dwemer ruin", "textures/mapmarkers/dwemer.png", { 0.98f, 0.65f, 0.30f } },
            { "daedric", "Daedric ruin", "textures/mapmarkers/daedric.png", { 0.94f, 0.44f, 0.40f } },
            { "stronghold", "Stronghold", "textures/mapmarkers/stronghold.png", { 0.81f, 0.67f, 0.54f } },
            { "fort", "Fort", "textures/mapmarkers/fort.png", { 0.80f, 0.85f, 0.91f } },
            { "tower", "Tower", "textures/mapmarkers/tower.png", { 0.77f, 0.65f, 0.91f } },
            { "ruin", "Ruin", "textures/mapmarkers/ruin.png", { 0.74f, 0.73f, 0.68f } },
            { "camp", "Camp", "textures/mapmarkers/camp.png", { 0.73f, 0.88f, 0.49f } },
            { "ship", "Ship", "textures/mapmarkers/ship.png", { 0.48f, 0.83f, 0.91f } },
            { "shipwreck", "Shipwreck", "textures/mapmarkers/shipwreck.png", { 0.42f, 0.69f, 0.83f } },
            { "landmark", "Landmark", "textures/mapmarkers/landmark.png", { 0.91f, 0.88f, 0.73f } },
            { "misc", "Other location", "textures/mapmarkers/misc.png", { 0.83f, 0.83f, 0.83f } },
        } };

        struct KnownEntry
        {
            std::uint32_t offset;
            std::uint16_t length;
            Category category;
        };

        // Generated from the attributed location/type facts, without coordinates.
#include "knownlocations.inc"

        constexpr char lowerAscii(char value)
        {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
        }

        constexpr bool asciiSpace(char value)
        {
            return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' || value == '\v';
        }

        constexpr bool asciiLetter(char value)
        {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
        }

        std::string_view trim(std::string_view value)
        {
            while (!value.empty() && asciiSpace(value.front()))
                value.remove_prefix(1);
            while (!value.empty() && asciiSpace(value.back()))
                value.remove_suffix(1);
            return value;
        }

        // All catalogue names fit in 47 bytes. Mod names up to 256 bytes use
        // only stack storage; longer names remain complete through the fallback.
        class NormalizedName
        {
            std::array<char, 256> mBuffer;
            std::string mOverflow;
            std::size_t mSize = 0;
            bool mLarge;

        public:
            explicit NormalizedName(std::string_view input)
                : mLarge(input.size() > mBuffer.size())
            {
                if (mLarge)
                    mOverflow.resize(input.size());
                char* const output = mLarge ? mOverflow.data() : mBuffer.data();
                bool space = false;
                for (std::size_t i = 0; i < input.size(); ++i)
                {
                    char value = input[i];
                    if (asciiSpace(value))
                    {
                        space = mSize != 0;
                        continue;
                    }
                    if (input.substr(i, 3) == "\xE2\x80\x99")
                    {
                        value = '\'';
                        i += 2;
                    }
                    else if (input.substr(i, 3) == "\xEF\xBC\x8C")
                    {
                        value = ',';
                        i += 2;
                    }
                    if (space)
                        output[mSize++] = ' ';
                    output[mSize++] = lowerAscii(value);
                    space = false;
                }
            }

            std::string_view view() const
            {
                return { mLarge ? mOverflow.data() : mBuffer.data(), mSize };
            }
        };

        std::optional<Category> known(std::string_view name)
        {
            if (name.empty())
                return std::nullopt;
            const auto found = std::lower_bound(sKnownNames.begin(), sKnownNames.end(), name,
                [](const KnownEntry& entry, std::string_view value) {
                    return std::string_view(sKnownNamePool + entry.offset, entry.length) < value;
                });
            if (found == sKnownNames.end() || std::string_view(sKnownNamePool + found->offset, found->length) != name)
                return std::nullopt;
            return found->category;
        }

        bool word(std::string_view text, std::string_view value)
        {
            std::size_t start = 0;
            while ((start = text.find(value, start)) != std::string_view::npos)
            {
                const std::size_t end = start + value.size();
                if ((start == 0 || !asciiLetter(text[start - 1])) && (end == text.size() || !asciiLetter(text[end])))
                    return true;
                ++start;
            }
            return false;
        }

        bool words(std::string_view text, std::initializer_list<std::string_view> values)
        {
            return std::any_of(values.begin(), values.end(), [text](std::string_view value) { return word(text, value); });
        }

        std::optional<Category> preciseName(std::string_view value)
        {
            if (words(value, { "shipwreck", "wreck", "wreckage" }))
                return Category::Shipwreck;
            if (words(value, { "barrow", "burial mound" }))
                return Category::Barrow;
            if (words(value, { "tomb", "crypt", "catacombs", "mausoleum" }))
                return Category::Tomb;
            if (words(value, { "mine", "eggmine", "quarry" }))
                return Category::Mine;
            if (words(value, { "dwemer", "dwarven" }))
                return Category::Dwemer;
            if (words(value, { "daedric", "daedra" }))
                return Category::Daedric;
            return std::nullopt;
        }

        std::optional<Category> broadName(std::string_view value)
        {
            if (words(value, { "cave", "caves", "cavern", "caverns", "grotto", "grottos", "grottoes", "cavernous" }))
                return Category::Cave;
            if (word(value, "stronghold"))
                return Category::Stronghold;
            if (words(value, { "fort", "fortress", "castle" }))
                return Category::Fort;
            if (words(value, { "tower", "lighthouse", "watchtower" }))
                return Category::Tower;
            if (words(value, { "ruin", "ruins" }))
                return Category::Ruin;
            if (words(value, { "camp", "campsite" }))
                return Category::Camp;
            if (words(value, { "ship", "boat", "galleon" }))
                return Category::Ship;
            if (words(value, { "farm", "plantation", "village", "hamlet" }))
                return Category::Settlement;
            if (words(value, { "house", "shack", "manor", "mansion", "hut", "farmhouse", "inn", "tavern", "shop", "store" }))
                return Category::House;
            if (words(value, { "mount", "mountain", "peak", "monument", "statue" }))
                return Category::Landmark;
            return std::nullopt;
        }

        std::optional<Category> modelCategory(std::string_view model)
        {
            const std::size_t slash = model.find_last_of("/\\");
            const std::string_view filename = model.substr(slash == std::string_view::npos ? 0 : slash + 1);
            const auto prefix = [filename](std::string_view value) {
                return filename.size() >= value.size()
                    && std::equal(value.begin(), value.end(), filename.begin(),
                        [](char expected, char actual) { return expected == lowerAscii(actual); });
            };
            if (prefix("in_tomb_") || prefix("ex_tomb_"))
                return Category::Tomb;
            if (prefix("in_bm_barrow") || prefix("in_barrow_"))
                return Category::Barrow;
            // "de" is also used for ordinary Dunmer doors; Daedric uses "dae".
            if (prefix("in_dae_") || prefix("ex_dae_"))
                return Category::Daedric;
            if (prefix("in_dwrv_") || prefix("ex_dwrv_"))
                return Category::Dwemer;
            if (prefix("in_stronghold_") || prefix("ex_stronghold_") || prefix("in_strong_") || prefix("ex_strong_"))
                return Category::Stronghold;
            if (prefix("in_cave_") || prefix("ex_cave_") || prefix("in_moldcave_") || prefix("in_lavacave_"))
                return Category::Cave;
            return std::nullopt;
        }

        std::optional<std::string_view> parent(std::string_view name)
        {
            const std::size_t comma = name.find(',');
            if (comma == std::string_view::npos)
                return std::nullopt;
            return trim(name.substr(0, comma));
        }

        std::string_view destination(std::string_view name, bool placeLabel)
        {
            const std::size_t comma = name.find(',');
            return placeLabel || comma == std::string_view::npos ? name : trim(name.substr(comma + 1));
        }
    }

    const Definition& definition(Category category) noexcept
    {
        return sDefinitions[static_cast<std::size_t>(isValid(category) ? category : Category::Misc)];
    }

    Category classify(std::string_view cellId, std::string_view displayName, std::string_view doorModel, bool placeLabel)
    {
        const NormalizedName normalizedId(cellId);
        const NormalizedName normalizedDisplay(displayName);
        const std::array<std::string_view, 2> candidates{ normalizedId.view(), normalizedDisplay.view() };
        const auto parentLabel = [&candidates, placeLabel](std::string_view name) {
            if (placeLabel || name.empty())
                return false;
            for (std::string_view full : candidates)
                if (parent(full) == name)
                    return true;
            return false;
        };

        // A source-authored exact location beats ambiguous keywords and models.
        for (std::string_view name : candidates)
            if (!parentLabel(name))
                if (const auto category = known(name))
                    return *category;

        for (std::string_view name : candidates)
            if (!parentLabel(name))
                if (const auto category = preciseName(destination(name, placeLabel)))
                    return *category;

        if (!placeLabel)
            if (const auto category = modelCategory(doorModel))
                return *category;

        for (std::string_view name : candidates)
            if (!parentLabel(name))
                if (const auto category = broadName(destination(name, placeLabel)))
                    return *category;

        for (std::string_view name : candidates)
            if (const auto prefix = parent(name))
                if (const auto category = known(*prefix))
                {
                    if (!placeLabel && (*category == Category::City || *category == Category::Town
                                           || *category == Category::Settlement))
                        return Category::House;
                    return *category;
                }
        return Category::Misc;
    }
}
