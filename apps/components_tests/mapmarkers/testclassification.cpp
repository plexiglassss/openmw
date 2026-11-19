#include <gtest/gtest.h>

#include "cases.hpp"

#include <set>
#include <string>

namespace
{
    using MapMarkers::Category;

    TEST(MapMarkers, ExactFactsAndConservativeFallbacks)
    {
        for (const MapMarkersTest::Case& value : MapMarkersTest::cases)
        {
            SCOPED_TRACE(std::string(value.cellId) + "/" + std::string(value.displayName) + "/"
                + std::string(value.doorModel));
            EXPECT_EQ(MapMarkers::classify(value.cellId, value.displayName, value.doorModel, value.placeLabel),
                value.expected);
        }
    }

    TEST(MapMarkers, CompleteCategoryMetadataAndIndependentBits)
    {
        std::set<std::string_view> ids;
        std::set<std::string_view> textures;
        std::uint32_t mask = 0;
        for (std::size_t i = 0; i < MapMarkers::categoryCount; ++i)
        {
            const auto category = static_cast<Category>(i);
            const auto& definition = MapMarkers::definition(category);
            EXPECT_TRUE(MapMarkers::isValid(category));
            EXPECT_TRUE(ids.insert(definition.id).second);
            EXPECT_FALSE(definition.label.empty());
            EXPECT_TRUE(textures.insert(definition.texturePath).second);
            EXPECT_EQ(definition.texturePath, "textures/mapmarkers/" + std::string(definition.id) + ".png");
            for (float channel : definition.color)
            {
                EXPECT_GE(channel, 0.f);
                EXPECT_LE(channel, 1.f);
            }
            EXPECT_EQ(mask & MapMarkers::bit(category), 0u);
            mask |= MapMarkers::bit(category);
        }
        EXPECT_EQ(MapMarkers::categoryCount, 19u);
        EXPECT_EQ(mask, 0x7ffffu);
        EXPECT_EQ(static_cast<unsigned>(Category::City), 0u);
        EXPECT_EQ(static_cast<unsigned>(Category::Misc), 18u);
    }

    TEST(MapMarkers, InvalidSavedCategoriesCannotIndexOrShiftOutOfRange)
    {
        for (unsigned value : { 19u, 31u, 32u, 127u, 255u })
        {
            const auto category = static_cast<Category>(value);
            EXPECT_FALSE(MapMarkers::isValid(category));
            EXPECT_FALSE(MapMarkers::meaningfulEntrance(category));
            EXPECT_EQ(MapMarkers::bit(category), 0u);
            EXPECT_EQ(MapMarkers::definition(category).id, "misc");
        }
    }

    TEST(MapMarkers, OrdinaryDoorsDoNotQualifyForAutomaticEntranceMarkers)
    {
        for (Category category : { Category::City, Category::Town, Category::Settlement, Category::House, Category::Misc })
            EXPECT_FALSE(MapMarkers::meaningfulEntrance(category));
        for (Category category : { Category::Cave, Category::Mine, Category::Tomb, Category::Dwemer, Category::Camp })
            EXPECT_TRUE(MapMarkers::meaningfulEntrance(category));
    }

    TEST(MapMarkers, LongNamesAndNormalizationBoundariesAreNotTruncated)
    {
        EXPECT_EQ(MapMarkers::classify(std::string(512, 'q') + " Cave"), Category::Cave);
        EXPECT_EQ(MapMarkers::classify(std::string(512, 'q')), Category::Misc);
        EXPECT_EQ(MapMarkers::classify(std::string(256, ' ') + "Balmora  "), Category::City);
        EXPECT_EQ(MapMarkers::classify(std::string(251, 'q') + " Cave"), Category::Cave);
        EXPECT_EQ(MapMarkers::classify(std::string(252, 'q') + " Cave"), Category::Cave);
        EXPECT_EQ(MapMarkers::classify(std::string("Unknown\0Cave", 12)), Category::Cave);
    }
}
