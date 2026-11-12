#include <components/mapmarkers/icons.hpp>

#include <gtest/gtest.h>

#include <sstream>

namespace
{
    TEST(MapIconSkinTest, OrderedDestinationAndDoorRecordFallbacks)
    {
        std::istringstream rules("world\ttown\nlocal\tactive_door\nlocal\tshop\nlocal\tbooks\nlocal\tcave\n"
            "name\tshop\t shop\nname\tbooks\tbook shop\nname\tmissing\tbook shop\ndoor\tcave\tcave\r\n");
        const auto skin = MapMarkers::IconSet::read(rules, [](std::string_view) { return true; });
        const auto door = [&](std::string_view name, std::string_view model, bool exterior) -> const std::string& {
            return skin.door(skin.classifyDoor(name, model, exterior)).vanilla;
        };
        EXPECT_EQ(door("A BOOK SHOP", "CaveDoor", true), "textures/mapmarkers/vanilla/local/books.dds");
        EXPECT_EQ(door("A BOOK SHOP", "CaveDoor", false), "textures/mapmarkers/vanilla/local/cave.dds");
        EXPECT_EQ(door("A Shop", "", true), "textures/mapmarkers/vanilla/local/shop.dds");
        EXPECT_EQ(door("Shopfront", "", true), "textures/mapmarkers/vanilla/local/active_door.dds");
        EXPECT_EQ(skin.world(MapMarkers::Category::Town).select(false), "textures/mapmarkers/vanilla/world/town.dds");
        EXPECT_EQ(skin.world(MapMarkers::Category::Town).select(true), "textures/mapmarkers/type/world/town.png");
        EXPECT_TRUE(skin.door(999).vanilla.empty());
    }

    TEST(MapIconSkinTest, MissingTypedArtKeepsOriginalAndMissingOriginalKeepsFallback)
    {
        std::istringstream rules("world\tcity\nworld\tcave\nlocal\tactive_door\nlocal\tcave\nlocal\t../bad\n"
            "name\tcave\tcave\ndoor\t../bad\tbroken\n");
        const auto skin = MapMarkers::IconSet::read(rules, [](std::string_view path) {
            return path.starts_with("textures/mapmarkers/vanilla/") && !path.ends_with("cave.dds");
        });
        EXPECT_EQ(skin.world(MapMarkers::Category::City).select(true), skin.world(MapMarkers::Category::City).select(false));
        EXPECT_FALSE(skin.world(MapMarkers::Category::City).vanilla.empty());
        EXPECT_TRUE(skin.world(MapMarkers::Category::Cave).select(true).empty());
        EXPECT_EQ(skin.door(skin.classifyDoor("cave", "broken", true)).vanilla, "textures/mapmarkers/vanilla/local/active_door.dds");
    }

    TEST(MapIconSkinTest, MaximumAppliesAfterZoomAndClusteringAndPreservesVisibility)
    {
        EXPECT_FLOAT_EQ(MapMarkers::locationIconExtent(24, 4, 0, false, 16), 16);
        EXPECT_FLOAT_EQ(MapMarkers::locationIconExtent(24, 4, 0, true, 32), 32);
        EXPECT_FLOAT_EQ(MapMarkers::locationIconExtent(24, 0.5f, 25, false, 20), 20);
        EXPECT_FLOAT_EQ(MapMarkers::locationIconExtent(24, 0.5f, 1, false, 24), 12);
        EXPECT_FLOAT_EQ(MapMarkers::locationIconExtent(24, 0.5f, 0, false, 24), 0);
        EXPECT_FLOAT_EQ(MapMarkers::locationIconExtent(24, 1, 2, false, 24), 0);
        EXPECT_EQ(MapMarkers::localIconExtent(15, 1, 24), 15);
        EXPECT_EQ(MapMarkers::localIconExtent(15, 4, 24), 24);
        EXPECT_EQ(MapMarkers::localIconExtent(15, 0.5f, 8), 8);
        EXPECT_EQ(MapMarkers::localIconExtent(40, 1, 8), 8);
    }
}
