#include <components/esm/fourcc.hpp>
#include <components/esm3/dungeonstates.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace ESM
{
    namespace
    {
        constexpr std::uint32_t sentinelId = fourCC("NEXT");
        constexpr std::uint32_t sentinelValue = 0x1234abcd;

        std::unique_ptr<std::istream> streamWithStates(const std::function<void(ESMWriter&)>& writeStates)
        {
            auto stream = std::make_unique<std::stringstream>();
            ESMWriter writer;
            writer.setFormatVersion(CurrentSaveGameFormatVersion);
            writer.setRecordCount(2);
            writer.save(*stream);
            writer.startRecord(REC_DCLR);
            writeStates(writer);
            writer.endRecord(REC_DCLR);
            writer.startRecord(sentinelId);
            writer.writeHNT("DATA", sentinelValue);
            writer.endRecord(sentinelId);
            EXPECT_EQ(writer.getRecordCount(), 3); // Includes the TES3 header.
            return stream;
        }

        void openStates(ESMReader& reader, std::unique_ptr<std::istream> stream)
        {
            reader.open(std::move(stream), "native-cleared-dungeons-test");
            ASSERT_EQ(reader.getFormatVersion(), CurrentSaveGameFormatVersion);
            ASSERT_EQ(reader.getRecordCount(), 2);
            ASSERT_TRUE(reader.hasMoreRecs());
            ASSERT_EQ(reader.getRecName().toInt(), REC_DCLR);
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

        // Construct corrupt and future records independently of the production
        // writer, so these fixtures test reader validation and record bounds.
        void writeEntry(ESMWriter& writer, const RefId& cell, std::uint8_t status)
        {
            writer.writeHNRefId("NAME", cell);
            writer.writeHNT("STAT", status);
        }

        TEST(Esm3DungeonStatesTest, unknownDoorLookupDoesNotCreateHistory)
        {
            DungeonStates states;
            EXPECT_FALSE(states.isCleared(RefId::stringRefId("Unvisited cave")));
            EXPECT_FALSE(states.isCleared(RefId()));
            EXPECT_TRUE(states.mStates.empty());
        }

        TEST(Esm3DungeonStatesTest, enteringWithHostilesAndKillingTheLastOneClearsOnlyThatRoom)
        {
            const RefId first = RefId::stringRefId("Cave, Entrance");
            const RefId second = RefId::stringRefId("Cave, Lower Level");
            DungeonStates states;
            EXPECT_TRUE(states.observe(first, true));
            EXPECT_TRUE(states.observe(second, true));
            EXPECT_EQ(states.mStates.at(first), DungeonStatus::Uncleared);
            EXPECT_FALSE(states.isCleared(first));
            // One death with another living hostile does not mark it cleared.
            EXPECT_FALSE(states.observe(first, true));
            EXPECT_TRUE(states.observe(first, false));
            EXPECT_TRUE(states.isCleared(first));
            EXPECT_FALSE(states.isCleared(second));
            EXPECT_FALSE(states.observe(first, false));
        }

        TEST(Esm3DungeonStatesTest, initiallyEmptyOrPeacefulRoomsRemainIneligible)
        {
            const RefId cell = RefId::stringRefId("Balmora, Shop");
            DungeonStates states;
            EXPECT_TRUE(states.observe(cell, false));
            EXPECT_EQ(states.mStates.at(cell), DungeonStatus::Invalid);
            EXPECT_FALSE(states.isCleared(cell));
            // The source mod's invalid classification is intentionally sticky,
            // even if a hostile later enters and is killed in a peaceful shop.
            EXPECT_FALSE(states.observe(cell, true));
            EXPECT_FALSE(states.observe(cell, false));
            EXPECT_EQ(states.mStates.at(cell), DungeonStatus::Invalid);
        }

        TEST(Esm3DungeonStatesTest, respawnOrHostileActivationRevokesClearedUntilLastHostileIsGone)
        {
            const RefId cell = RefId::stringRefId("Ancestral Tomb");
            DungeonStates states;
            states.observe(cell, true);
            states.observe(cell, false);
            ASSERT_TRUE(states.isCleared(cell));
            // Both re-entry after a respawn and activation of a new living
            // hostile provide this same snapshot to the engine state machine.
            EXPECT_TRUE(states.observe(cell, true));
            EXPECT_FALSE(states.isCleared(cell));
            EXPECT_EQ(states.mStates.at(cell), DungeonStatus::Uncleared);
            EXPECT_TRUE(states.observe(cell, false));
            EXPECT_TRUE(states.isCleared(cell));
        }

        TEST(Esm3DungeonStatesTest, reentryWithoutHostilesClearsPreviouslyUnclearedRoom)
        {
            const RefId cell = RefId::stringRefId("Bandit Cave");
            DungeonStates states;
            states.observe(cell, true);
            // Covers a hostile removed while the room was not active. No
            // inactive cell needs to be loaded merely to maintain its status.
            EXPECT_TRUE(states.observe(cell, false));
            EXPECT_TRUE(states.isCleared(cell));
        }

        TEST(Esm3DungeonStatesTest, caseInsensitiveCellIdentityDoesNotCreateSeparateRooms)
        {
            DungeonStates states;
            states.observe(RefId::stringRefId("Samarys Ancestral Tomb"), true);
            states.observe(RefId::stringRefId("samarys ancestral tomb"), false);
            ASSERT_EQ(states.mStates.size(), 1);
            EXPECT_TRUE(states.isCleared(RefId::stringRefId("SAMARYS ANCESTRAL TOMB")));
        }

        TEST(Esm3DungeonStatesTest, emptyCellIdentityCannotCreateState)
        {
            DungeonStates states;
            EXPECT_FALSE(states.observe(RefId(), true));
            EXPECT_FALSE(states.observe(RefId(), false));
            EXPECT_TRUE(states.mStates.empty());
        }

        TEST(Esm3DungeonStatesTest, oldSaveWithoutHistoryDoesNotInventRetroactiveCompletion)
        {
            DungeonStates states;
            const RefId alreadyEmpty = RefId::stringRefId("Previously cleared before installing");
            EXPECT_FALSE(states.isCleared(alreadyEmpty));
            states.observe(alreadyEmpty, false);
            EXPECT_EQ(states.mStates.at(alreadyEmpty), DungeonStatus::Invalid);
        }

        TEST(Esm3DungeonStatesTest, allThreeStatesRoundTripWithoutConsumingFollowingRecord)
        {
            DungeonStates input;
            const RefId shop = RefId::stringRefId("Shop");
            const RefId cave = RefId::stringRefId("Cave");
            const RefId tomb = RefId::stringRefId("Tomb");
            input.observe(shop, false);
            input.observe(cave, true);
            input.observe(tomb, true);
            input.observe(tomb, false);

            ESMReader reader;
            openStates(reader, streamWithStates([&](ESMWriter& writer) { input.save(writer); }));
            DungeonStates output;
            output.load(reader);
            EXPECT_EQ(output.mStates, input.mStates);
            EXPECT_TRUE(output.isCleared(tomb));
            EXPECT_FALSE(output.isCleared(cave));
            EXPECT_FALSE(output.isCleared(shop));
            expectSentinel(reader);
        }

        TEST(Esm3DungeonStatesTest, reloadingSavedClearedRoomStillAllowsRespawnToRevokeIt)
        {
            const RefId cell = RefId::stringRefId("Respawning Tomb");
            DungeonStates input;
            input.observe(cell, true);
            input.observe(cell, false);
            ESMReader reader;
            openStates(reader, streamWithStates([&](ESMWriter& writer) { input.save(writer); }));
            DungeonStates output;
            output.load(reader);
            ASSERT_TRUE(output.isCleared(cell));
            EXPECT_TRUE(output.observe(cell, true));
            EXPECT_FALSE(output.isCleared(cell));
            expectSentinel(reader);
        }

        TEST(Esm3DungeonStatesTest, loadingAnotherSaveReplacesHistoryAndEmptyRecordClearsIt)
        {
            DungeonStates first;
            first.observe(RefId::stringRefId("Character One's Cave"), true);
            first.observe(RefId::stringRefId("Character One's Cave"), false);
            DungeonStates second;
            second.observe(RefId::stringRefId("Character Two's Tomb"), true);
            DungeonStates output;
            for (const DungeonStates* input : { &first, &second })
            {
                ESMReader reader;
                openStates(reader, streamWithStates([&](ESMWriter& writer) { input->save(writer); }));
                output.load(reader);
                EXPECT_EQ(output.mStates, input->mStates);
                expectSentinel(reader);
            }
            ESMReader reader;
            openStates(reader, streamWithStates([](ESMWriter& writer) { DungeonStates{}.save(writer); }));
            output.load(reader);
            EXPECT_TRUE(output.mStates.empty());
            expectSentinel(reader);
        }

        TEST(Esm3DungeonStatesTest, stringCellIdentitiesSurviveContentFileRemapping)
        {
            DungeonStates input;
            const RefId cell = RefId::stringRefId("Modded Cave, Moved Island");
            input.observe(cell, true);
            input.observe(cell, false);
            ESMReader reader;
            openStates(reader, streamWithStates([&](ESMWriter& writer) { input.save(writer); }));
            const std::map<int, int> mapping{ { 0, 4 }, { 1, 0 }, { 4, 7 } };
            reader.setContentFileMapping(&mapping);
            DungeonStates output;
            output.load(reader);
            EXPECT_EQ(output.mStates, input.mStates);
            EXPECT_TRUE(output.isCleared(cell));
            expectSentinel(reader);
        }

        TEST(Esm3DungeonStatesTest, unknownVersionSkipsRecordAndClearsPreviousHistory)
        {
            DungeonStates output;
            output.observe(RefId::stringRefId("Previous Save's Cave"), true);
            ESMReader reader;
            openStates(reader, streamWithStates([](ESMWriter& writer) {
                writer.writeHNT("VERS", std::uint32_t(999));
                writer.writeHNString("NEW_", "future unrecognized format");
            }));
            output.load(reader);
            EXPECT_TRUE(output.mStates.empty());
            expectSentinel(reader);
        }

        TEST(Esm3DungeonStatesTest, olderEngineCanSkipIsolatedRecordAndReadFollowingData)
        {
            DungeonStates input;
            input.observe(RefId::stringRefId("Cave"), true);
            input.observe(RefId::stringRefId("Cave"), false);
            ESMReader reader;
            openStates(reader, streamWithStates([&](ESMWriter& writer) { input.save(writer); }));
            // This is the previous StateManager's default unknown-record path,
            // using the existing reader and no cleared-state parser at all.
            reader.skipRecord();
            expectSentinel(reader);
        }

        TEST(Esm3DungeonStatesTest, rejectsMissingOrWrongSizedVersion)
        {
            for (const bool missing : { true, false })
            {
                SCOPED_TRACE(missing);
                ESMReader reader;
                openStates(reader, streamWithStates([&](ESMWriter& writer) {
                    if (missing)
                        writeEntry(writer, RefId::stringRefId("Cave"), 2);
                    else
                        writer.writeHNT("VERS", std::uint16_t(1));
                }));
                DungeonStates output;
                EXPECT_THROW(output.load(reader), std::runtime_error);
            }
        }

        TEST(Esm3DungeonStatesTest, rejectsEmptyIdentityAndUnknownState)
        {
            for (const bool emptyIdentity : { true, false })
            {
                SCOPED_TRACE(emptyIdentity);
                ESMReader reader;
                openStates(reader, streamWithStates([&](ESMWriter& writer) {
                    writer.writeHNT("VERS", std::uint32_t(1));
                    writeEntry(writer, emptyIdentity ? RefId() : RefId::stringRefId("Cave"), emptyIdentity ? 2 : 3);
                }));
                DungeonStates output;
                EXPECT_THROW(output.load(reader), std::runtime_error);
            }
        }

        TEST(Esm3DungeonStatesTest, rejectsDuplicateIdentityEvenWithDifferentCapitalization)
        {
            ESMReader reader;
            openStates(reader, streamWithStates([](ESMWriter& writer) {
                writer.writeHNT("VERS", std::uint32_t(1));
                writeEntry(writer, RefId::stringRefId("Cave"), 1);
                writeEntry(writer, RefId::stringRefId("CAVE"), 2);
            }));
            DungeonStates output;
            EXPECT_THROW(output.load(reader), std::runtime_error);
        }

        TEST(Esm3DungeonStatesTest, rejectsEntryWithoutState)
        {
            ESMReader reader;
            openStates(reader, streamWithStates([](ESMWriter& writer) {
                writer.writeHNT("VERS", std::uint32_t(1));
                writer.writeHNRefId("NAME", RefId::stringRefId("Cave"));
            }));
            DungeonStates output;
            EXPECT_THROW(output.load(reader), std::runtime_error);
        }

        TEST(Esm3DungeonStatesTest, writerRejectsInvalidInternalStates)
        {
            for (const bool emptyIdentity : { true, false })
            {
                SCOPED_TRACE(emptyIdentity);
                DungeonStates input;
                input.mStates.emplace(emptyIdentity ? RefId() : RefId::stringRefId("Cave"),
                    emptyIdentity ? DungeonStatus::Cleared : static_cast<DungeonStatus>(3));
                EXPECT_THROW(streamWithStates([&](ESMWriter& writer) { input.save(writer); }), std::runtime_error);
            }
        }
    }
}
