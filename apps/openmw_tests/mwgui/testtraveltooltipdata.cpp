#include <gtest/gtest.h>

#include <apps/openmw/mwgui/traveltooltipdata.hpp>

#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
    using namespace MWGui::TravelTooltips;

    Data parse(std::string_view text)
    {
        std::istringstream stream{ std::string(text) };
        return readData(stream);
    }

    const auto allImages = [](std::string_view) { return true; };

    TEST(TravelTooltips, ReadsEscapesCrLfAndNormalizesTextureNames)
    {
        const Data data = parse("# Fixture with no third-party content\r\nversion\t1\r\n"
                                "description\tExample\tFirst\\nSecond\\tTabbed\\\\Slash\r\n"
                                "image\t2\tExample\tTextures/Example/Image.TGA\t1200\t600\r\n"
                                "overview\t0\ttextures/example/overview.tga\t512\t512\r\n");
        ASSERT_EQ(data.mImages.size(), 1u);
        EXPECT_EQ(data.mImages[0].mPath, "textures/example/image.tga");
        EXPECT_EQ(data.mImages[0].mWidth, 1200);
        EXPECT_EQ(destinationDescription(data, "Example, Guild", false), "First\nSecond\tTabbed\\Slash");
        ASSERT_NE(overviewImage(data, 0, allImages), nullptr);
        EXPECT_EQ(overviewImage(data, 1, allImages), nullptr);
    }

    TEST(TravelTooltips, LongestPrefixAndGondolierDescriptionsAreSeparate)
    {
        const Data data = parse("version\t1\n"
                                "description\tExample\tTown\n"
                                "description\tExample Long\tDistrict\n"
                                "gondolier\tLong\tCanal\n"
                                "image\t0\tExample\ttextures/example.tga\t400\t200\n"
                                "image\t0\tExample Long\ttextures/long.tga\t400\t200\n");
        EXPECT_EQ(destinationDescription(data, "EXAMPLE LONG, Guild", false), "District");
        EXPECT_EQ(destinationDescription(data, "Example Long", true), "Canal");
        EXPECT_EQ(destinationDescription(data, "Unmapped", false), "");
        const auto image = destinationImage(data, "Example Long, Guild", 0, false, 0, allImages);
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->mPath, "textures/long.tga");
    }

    TEST(TravelTooltips, MissingAssetsAndOptionalFallbackUseOnlyStuporstarSlot)
    {
        const Data data = parse("version\t1\n"
                                "image\t0\tExample\ttextures/fallback.tga\t400\t200\n"
                                "image\t1\tOther\ttextures/other.tga\t400\t200\n"
                                "image\t2\tExample\ttextures/missing.tga\t400\t200\n");
        const auto available = [](std::string_view path) { return path != "textures/missing.tga"; };
        EXPECT_EQ(destinationImage(data, "Example", 2, false, 0, available), nullptr);
        const auto fallback = destinationImage(data, "Example", 2, true, 0, available);
        ASSERT_NE(fallback, nullptr);
        EXPECT_EQ(fallback->mPath, "textures/fallback.tga");
        EXPECT_EQ(destinationImage(data, "Other", 2, true, 0, available), nullptr);
        EXPECT_EQ(destinationImage(data, "Example", -1, true, 0, available), nullptr);
        EXPECT_EQ(destinationImage(data, "Example", 3, true, 0, available), nullptr);
    }

    TEST(TravelTooltips, VariantDoesNotChangeWhileHoveredAndAllVariantsAreReachable)
    {
        const Data data = parse("version\t1\n"
                                "image\t2\tExample\ttextures/a.tga\t400\t200\n"
                                "image\t2\tExample\ttextures/b.tga\t400\t200\n"
                                "image\t2\tExample\ttextures/c.tga\t400\t200\n");
        std::set<std::string> seen;
        for (std::uint64_t seed = 0; seed < 100; ++seed)
        {
            const auto first = destinationImage(data, "Example", 2, false, seed, allImages);
            ASSERT_NE(first, nullptr);
            EXPECT_EQ(first, destinationImage(data, "Example", 2, false, seed, allImages));
            seen.insert(first->mPath);
        }
        EXPECT_EQ(seen.size(), 3u);
    }

    TEST(TravelTooltips, OverviewExcludesKnownOtherProvinceProviders)
    {
        EXPECT_TRUE(hasVvardenfellOverview("example_sailor"));
        EXPECT_FALSE(hasVvardenfellOverview("TR_example"));
        EXPECT_FALSE(hasVvardenfellOverview("pc_example"));
        EXPECT_FALSE(hasVvardenfellOverview("Sky_example"));
    }

    TEST(TravelTooltips, RejectsMalformedOrUnboundedManifest)
    {
        for (const std::string_view fixture : { "", "version\t2\n", "description\tTown\tText\n",
                 "version\t1\nunknown\t1\n", "version\t1\ndescription\t\tText\n",
                 "version\t1\ndescription\tTown\tbad\\q\n",
                 "version\t1\nimage\t0\tTown\ttextures/../escape.tga\t10\t10\n",
                 "version\t1\nimage\t0\tTown\tC:/textures/a.tga\t10\t10\n",
                 "version\t1\nimage\t0\tTown\ttextures//a.tga\t10\t10\n",
                 "version\t1\nimage\t0\tTown\ttextures/a.tga\t0\t10\n",
                 "version\t1\nimage\t3\tTown\ttextures/a.tga\t10\t10\n",
                 "version\t1\noverview\t0\ttextures/a.tga\t10\t10\noverview\t0\ttextures/b.tga\t10\t10\n" })
        {
            SCOPED_TRACE(std::string(fixture));
            EXPECT_THROW(parse(fixture), std::runtime_error);
        }
        EXPECT_THROW(parse("version\t1\ndescription\tTown\t" + std::string(65536, 'a')), std::runtime_error);
        std::string nul = "version\t1\ndescription\tTown\tText";
        nul += '\0';
        EXPECT_THROW(parse(nul), std::runtime_error);
    }
}
