#include <components/esm/fourcc.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/globalmap.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ESM
{
    namespace
    {
        constexpr std::uint32_t sentinelId = fourCC("NEXT");
        constexpr std::uint32_t sentinelValue = 0x1234abcd;

        std::unique_ptr<std::istream> streamWithMarkers(const std::function<void(ESMWriter&)>& writeMarkers)
        {
            auto stream = std::make_unique<std::stringstream>();
            ESMWriter writer;
            writer.setFormatVersion(CurrentSaveGameFormatVersion);
            writer.setRecordCount(2);
            writer.save(*stream);
            writer.startRecord(REC_LMRK);
            writeMarkers(writer);
            writer.endRecord(REC_LMRK);
            writer.startRecord(sentinelId);
            writer.writeHNT("DATA", sentinelValue);
            writer.endRecord(sentinelId);
            EXPECT_EQ(writer.getRecordCount(), 3); // Includes the TES3 header.
            return stream;
        }

        void openMarkers(ESMReader& reader, std::unique_ptr<std::istream> stream)
        {
            reader.open(std::move(stream), "native-location-markers-test");
            ASSERT_EQ(reader.getFormatVersion(), CurrentSaveGameFormatVersion);
            ASSERT_EQ(reader.getRecordCount(), 2);
            ASSERT_TRUE(reader.hasMoreRecs());
            ASSERT_EQ(reader.getRecName().toInt(), REC_LMRK);
            reader.getRecHeader();
        }

        void expectSentinel(ESMReader& reader)
        {
            ASSERT_FALSE(reader.hasMoreSubs());
            ASSERT_TRUE(reader.hasMoreRecs());
            ASSERT_EQ(reader.getRecName().toInt(), sentinelId);
            reader.getRecHeader();
            std::uint32_t value = 0;
            reader.getHNT(value, "DATA");
            EXPECT_EQ(value, sentinelValue);
            EXPECT_FALSE(reader.hasMoreSubs());
            EXPECT_FALSE(reader.hasMoreRecs());
        }

        LocationMarker sampleMarker()
        {
            LocationMarker marker;
            marker.mDestination = RefId::stringRefId("Samarys Ancestral Tomb");
            marker.mDoor = RefId::stringRefId("ex_tomb_entrance");
            marker.mSourceX = -13;
            marker.mSourceY = 6;
            marker.mWorldX = -100000.25f;
            marker.mWorldY = 50000.5f;
            return marker;
        }

        void expectMarker(const LocationMarker& actual, const LocationMarker& expected)
        {
            EXPECT_EQ(actual.mDestination, expected.mDestination);
            EXPECT_EQ(actual.mDoor, expected.mDoor);
            EXPECT_EQ(actual.mSourceX, expected.mSourceX);
            EXPECT_EQ(actual.mSourceY, expected.mSourceY);
            EXPECT_FLOAT_EQ(actual.mWorldX, expected.mWorldX);
            EXPECT_FLOAT_EQ(actual.mWorldY, expected.mWorldY);
        }

        // This independent wire fixture bypasses save() so invalid inputs really
        // exercise the reader, rather than being rejected by the writer first.
        void writeEntry(ESMWriter& writer, const LocationMarker& marker)
        {
            writer.writeHNRefId("NAME", marker.mDestination);
            writer.writeHNRefId("DOOR", marker.mDoor);
            const std::array<std::int32_t, 2> grid{ marker.mSourceX, marker.mSourceY };
            writer.writeHNT("GRID", grid);
            const std::array<float, 2> position{ marker.mWorldX, marker.mWorldY };
            writer.writeHNT("POS_", position);
        }

        TEST(Esm3LocationMarkersTest, emptyRecordRoundTripDoesNotConsumeFollowingRecord)
        {
            LocationMarkers input;
            ESMReader reader;
            openMarkers(reader, streamWithMarkers([&](ESMWriter& writer) { input.save(writer); }));
            LocationMarkers output;
            output.load(reader);
            EXPECT_TRUE(output.mMarkers.empty());
            expectSentinel(reader);
        }

        TEST(Esm3LocationMarkersTest, preservesExactDestinationDoorNegativeGridAndFractionalPosition)
        {
            LocationMarkers input;
            input.mMarkers.push_back(sampleMarker());
            LocationMarker second = sampleMarker();
            second.mDestination = RefId::stringRefId("Some relocated cave");
            second.mDoor = RefId::stringRefId("custom_cave_door");
            second.mSourceX = 217;
            second.mSourceY = -153;
            second.mWorldX = 1777888.75f;
            second.mWorldY = -1253000.5f;
            input.mMarkers.push_back(second);
            ESMReader reader;
            openMarkers(reader, streamWithMarkers([&](ESMWriter& writer) { input.save(writer); }));
            LocationMarkers output;
            output.load(reader);
            ASSERT_EQ(output.mMarkers.size(), input.mMarkers.size());
            for (std::size_t i = 0; i < input.mMarkers.size(); ++i)
                expectMarker(output.mMarkers[i], input.mMarkers[i]);
            expectSentinel(reader);
        }

        TEST(Esm3LocationMarkersTest, stringIdentitiesSurviveContentFileRemapping)
        {
            LocationMarkers input;
            input.mMarkers.push_back(sampleMarker());
            ESMReader reader;
            openMarkers(reader, streamWithMarkers([&](ESMWriter& writer) { input.save(writer); }));
            const std::map<int, int> mapping{ { 0, 4 }, { 1, 0 }, { 4, 7 } };
            reader.setContentFileMapping(&mapping);
            LocationMarkers output;
            output.load(reader);
            ASSERT_EQ(output.mMarkers.size(), 1);
            expectMarker(output.mMarkers.front(), input.mMarkers.front());
            expectSentinel(reader);
        }

        TEST(Esm3LocationMarkersTest, repeatedLoadsReplaceRatherThanAccumulateMarkers)
        {
            LocationMarkers input;
            input.mMarkers.push_back(sampleMarker());
            LocationMarkers output;
            for (int i = 0; i < 3; ++i)
            {
                ESMReader reader;
                openMarkers(reader, streamWithMarkers([&](ESMWriter& writer) { input.save(writer); }));
                output.load(reader);
                ASSERT_EQ(output.mMarkers.size(), 1);
            }
            ESMReader emptyReader;
            openMarkers(emptyReader, streamWithMarkers([](ESMWriter& writer) { LocationMarkers{}.save(writer); }));
            output.load(emptyReader);
            EXPECT_TRUE(output.mMarkers.empty());
        }

        TEST(Esm3LocationMarkersTest, unknownVersionSkipsRecordAndClearsPreviousEntries)
        {
            ESMReader reader;
            openMarkers(reader, streamWithMarkers([](ESMWriter& writer) {
                writer.writeHNT("VERS", std::uint32_t(999));
                writer.writeHNString("NEW_", "future format with unrecognized fields");
            }));
            LocationMarkers output;
            output.mMarkers.push_back(sampleMarker());
            output.load(reader);
            EXPECT_TRUE(output.mMarkers.empty());
            expectSentinel(reader);
        }

        TEST(Esm3LocationMarkersTest, oldStateManagerSkipPreservesFollowingRecord)
        {
            LocationMarkers input;
            input.mMarkers.push_back(sampleMarker());
            ESMReader reader;
            openMarkers(reader, streamWithMarkers([&](ESMWriter& writer) { input.save(writer); }));
            // The prior StateManager default branch calls this exact API for
            // an unknown top-level record. No new LMRK parser is used here.
            reader.skipRecord();
            expectSentinel(reader);
        }

        TEST(Esm3LocationMarkersTest, globalMapRecordStillEndsAtLegacyMarkers)
        {
            auto stream = std::make_unique<std::stringstream>();
            ESMWriter writer;
            writer.setFormatVersion(CurrentSaveGameFormatVersion);
            writer.setRecordCount(2);
            writer.save(*stream);
            GlobalMap input;
            input.mBounds = { -20, 30, -40, 50 };
            input.mImageData = { 'a', 'b', 'c', '\0', 'd' };
            input.mMarkers = { { -3, 17 }, { 25, -11 } };
            writer.startRecord(REC_GMAP);
            input.save(writer);
            writer.endRecord(REC_GMAP);
            writer.startRecord(REC_LMRK);
            LocationMarkers{}.save(writer);
            writer.endRecord(REC_LMRK);
            EXPECT_EQ(writer.getRecordCount(), 3);

            ESMReader reader;
            reader.open(std::move(stream), "native-marker-legacy-global-map");
            ASSERT_EQ(reader.getRecName().toInt(), REC_GMAP);
            reader.getRecHeader();
            GlobalMap output;
            output.load(reader);
            EXPECT_EQ(output.mImageData, input.mImageData);
            EXPECT_EQ(output.mMarkers, input.mMarkers);
            EXPECT_EQ(output.mBounds.mMinX, input.mBounds.mMinX);
            EXPECT_EQ(output.mBounds.mMaxX, input.mBounds.mMaxX);
            EXPECT_EQ(output.mBounds.mMinY, input.mBounds.mMinY);
            EXPECT_EQ(output.mBounds.mMaxY, input.mBounds.mMaxY);
            EXPECT_FALSE(reader.hasMoreSubs());
            ASSERT_EQ(reader.getRecName().toInt(), REC_LMRK);
            reader.getRecHeader();
            reader.skipRecord();
            EXPECT_FALSE(reader.hasMoreRecs());
        }

        TEST(Esm3LocationMarkersTest, rejectsMissingOrWrongSizedVersion)
        {
            for (const bool missing : { true, false })
            {
                SCOPED_TRACE(missing);
                ESMReader reader;
                openMarkers(reader, streamWithMarkers([&](ESMWriter& writer) {
                    if (missing)
                        writeEntry(writer, sampleMarker());
                    else
                        writer.writeHNT("VERS", std::uint16_t(1));
                }));
                LocationMarkers output;
                EXPECT_THROW(output.load(reader), std::runtime_error);
            }
        }

        TEST(Esm3LocationMarkersTest, rejectsEmptyDestinationAndDoorIdentities)
        {
            for (const bool emptyDestination : { true, false })
            {
                SCOPED_TRACE(emptyDestination);
                ESMReader reader;
                openMarkers(reader, streamWithMarkers([&](ESMWriter& writer) {
                    writer.writeHNT("VERS", std::uint32_t(1));
                    LocationMarker marker = sampleMarker();
                    if (emptyDestination)
                        marker.mDestination = RefId();
                    else
                        marker.mDoor = RefId();
                    writeEntry(writer, marker);
                }));
                LocationMarkers output;
                EXPECT_THROW(output.load(reader), std::runtime_error);
            }
        }

        TEST(Esm3LocationMarkersTest, rejectsNonFinitePositionComponents)
        {
            for (const float invalid : { std::numeric_limits<float>::infinity(),
                     -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
                for (const bool invalidX : { true, false })
                {
                    SCOPED_TRACE(invalid);
                    SCOPED_TRACE(invalidX);
                    ESMReader reader;
                    openMarkers(reader, streamWithMarkers([&](ESMWriter& writer) {
                        writer.writeHNT("VERS", std::uint32_t(1));
                        LocationMarker marker = sampleMarker();
                        (invalidX ? marker.mWorldX : marker.mWorldY) = invalid;
                        writeEntry(writer, marker);
                    }));
                    LocationMarkers output;
                    EXPECT_THROW(output.load(reader), std::runtime_error);
                }
        }

        TEST(Esm3LocationMarkersTest, rejectsWrongSizedGridAndPosition)
        {
            for (const bool shortGrid : { true, false })
            {
                SCOPED_TRACE(shortGrid);
                ESMReader reader;
                openMarkers(reader, streamWithMarkers([&](ESMWriter& writer) {
                    writer.writeHNT("VERS", std::uint32_t(1));
                    const LocationMarker marker = sampleMarker();
                    writer.writeHNRefId("NAME", marker.mDestination);
                    writer.writeHNRefId("DOOR", marker.mDoor);
                    if (shortGrid)
                        writer.writeHNT("GRID", marker.mSourceX);
                    else
                    {
                        const std::array<std::int32_t, 2> grid{ marker.mSourceX, marker.mSourceY };
                        writer.writeHNT("GRID", grid);
                    }
                    writer.writeHNT("POS_", marker.mWorldX);
                }));
                LocationMarkers output;
                EXPECT_THROW(output.load(reader), std::runtime_error);
            }
        }

        TEST(Esm3LocationMarkersTest, rejectsMissingRequiredDoorAndUnexpectedTrailingData)
        {
            for (const bool missingDoor : { true, false })
            {
                SCOPED_TRACE(missingDoor);
                ESMReader reader;
                openMarkers(reader, streamWithMarkers([&](ESMWriter& writer) {
                    writer.writeHNT("VERS", std::uint32_t(1));
                    const LocationMarker marker = sampleMarker();
                    if (missingDoor)
                    {
                        writer.writeHNRefId("NAME", marker.mDestination);
                        writer.writeHNT("GRID", std::array<std::int32_t, 2>{ 1, 2 });
                    }
                    else
                    {
                        writeEntry(writer, marker);
                        writer.writeHNT("JUNK", std::uint32_t(42));
                    }
                }));
                LocationMarkers output;
                EXPECT_THROW(output.load(reader), std::runtime_error);
            }
        }

        TEST(Esm3LocationMarkersTest, populatedRecordRemainsBoundedByWrittenEntries)
        {
            // This is a round-trip/identity stress fixture, not a target-PC FPS
            // benchmark. It catches truncation and count-driven over-allocation.
            LocationMarkers input;
            constexpr std::size_t count = 4096;
            input.mMarkers.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                LocationMarker marker = sampleMarker();
                marker.mDestination = RefId::stringRefId("fixture cave " + std::to_string(i));
                marker.mWorldX += static_cast<float>(i);
                input.mMarkers.push_back(std::move(marker));
            }
            ESMReader reader;
            openMarkers(reader, streamWithMarkers([&](ESMWriter& writer) { input.save(writer); }));
            LocationMarkers output;
            output.load(reader);
            ASSERT_EQ(output.mMarkers.size(), count);
            for (std::size_t i = 0; i < count; ++i)
                expectMarker(output.mMarkers[i], input.mMarkers[i]);
            expectSentinel(reader);
        }
    }
}
