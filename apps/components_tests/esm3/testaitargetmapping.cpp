#include <components/esm/fourcc.hpp>
#include <components/esm3/aisequence.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>

namespace ESM
{
    namespace
    {
        constexpr auto recordType = fourCC("TEST");
        constexpr std::uint32_t sentinel = 0x12345678;
        constexpr std::array modernFormats = { MaxActorIdSaveGameFormatVersion + 1, CurrentSaveGameFormatVersion };

        enum class TargetEncoding
        {
            Modern,
            Legacy,
            Omitted,
        };

        template <class Package>
        constexpr bool hasNamedTarget
            = std::is_same_v<Package, AiSequence::AiFollow> || std::is_same_v<Package, AiSequence::AiEscort>;

        template <class Package>
        Package makePackage(RefNum target)
        {
            Package package{};
            package.mTargetActor = target;
            if constexpr (hasNamedTarget<Package>)
            {
                package.mData = { 1.25f, -2.5f, 3.75f, 7 };
                package.mTargetId = RefId::stringRefId("fixture_target");
                package.mCellId = "Target Cell";
                package.mRemainingDuration = 12.5f;
                package.mRepeat = true;
            }
            if constexpr (std::is_same_v<Package, AiSequence::AiFollow>)
            {
                package.mAlwaysFollow = true;
                package.mCommanded = true;
                package.mActive = true;
            }
            return package;
        }

        // The current writer no longer emits legacy actor IDs. These fixtures encode the old 32-bit field explicitly,
        // or omit the optional target field, while retaining the fields surrounding it.
        template <class Package>
        void writeTargetFixture(ESMWriter& writer, const Package& package, TargetEncoding encoding)
        {
            if constexpr (hasNamedTarget<Package>)
            {
                writer.startSubRecord("DATA");
                writer.writeT(package.mData.mX);
                writer.writeT(package.mData.mY);
                writer.writeT(package.mData.mZ);
                writer.writeT(package.mData.mDuration);
                writer.endRecord("DATA");
                writer.writeHNRefId("TARG", package.mTargetId);
            }
            if (encoding == TargetEncoding::Legacy)
            {
                if constexpr (hasNamedTarget<Package>)
                    writer.writeHNT("TAID", package.mTargetActor.mIndex);
                else
                    writer.writeHNT("TARG", package.mTargetActor.mIndex);
            }
            if constexpr (hasNamedTarget<Package>)
            {
                writer.writeHNT("DURA", package.mRemainingDuration);
                writer.writeHNString("CELL", package.mCellId);
            }
            if constexpr (std::is_same_v<Package, AiSequence::AiFollow>)
            {
                writer.writeHNT("ALWY", package.mAlwaysFollow);
                writer.writeHNT("CMND", package.mCommanded);
                writer.writeHNT("ACTV", package.mActive);
            }
            if constexpr (hasNamedTarget<Package>)
                writer.writeHNT("REPT", package.mRepeat);
        }

        template <class Package>
        std::string savePackage(
            const Package& package, FormatVersion format, TargetEncoding encoding = TargetEncoding::Modern)
        {
            std::stringstream stream;
            ESMWriter writer;
            writer.setFormatVersion(format);
            writer.save(stream);
            writer.startRecord(recordType);
            if (encoding == TargetEncoding::Modern)
                package.save(writer);
            else
                writeTargetFixture(writer, package, encoding);
            writer.writeHNT("NEXT", sentinel);
            writer.endRecord(recordType);
            writer.close();
            return stream.str();
        }

        template <class Package>
        Package loadPackage(const std::string& data, const std::map<int, int>* mapping, RefNum previousTarget = {})
        {
            ESMReader reader;
            reader.open(std::make_unique<std::stringstream>(data), "AI target fixture");
            reader.setContentFileMapping(mapping);
            EXPECT_EQ(reader.getRecName().toInt(), recordType);
            reader.getRecHeader();
            Package package{};
            package.mTargetActor = previousTarget;
            package.load(reader);
            std::uint32_t next = 0;
            reader.getHNT(next, "NEXT");
            EXPECT_EQ(next, sentinel);
            EXPECT_FALSE(reader.hasMoreSubs());
            EXPECT_FALSE(reader.hasMoreRecs());
            return package;
        }

        template <class Package>
        class Esm3AiTargetMappingTest : public testing::Test
        {
        };

        using TargetPackages
            = testing::Types<AiSequence::AiFollow, AiSequence::AiEscort, AiSequence::AiCombat, AiSequence::AiPursue>;
        TYPED_TEST_SUITE(Esm3AiTargetMappingTest, TargetPackages);

        TYPED_TEST(Esm3AiTargetMappingTest, insertedContentRemapsTargetAndPreservesOtherFields)
        {
            const std::map<int, int> mapping{ { 100, 101 } };
            for (const auto format : modernFormats)
            {
                SCOPED_TRACE(format);
                const auto original = makePackage<TypeParam>({ 82, 100 });
                const auto result = loadPackage<TypeParam>(savePackage(original, format), &mapping);
                EXPECT_EQ(result.mTargetActor, (RefNum{ 82, 101 }));
                EXPECT_EQ(savePackage(result, format), savePackage(makePackage<TypeParam>({ 82, 101 }), format));
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, permutationIsAppliedExactlyOnce)
        {
            const std::map<int, int> mapping{ { 7, 3 }, { 3, 12 }, { 12, 7 } };
            for (const auto format : modernFormats)
            {
                SCOPED_TRACE(format);
                for (const auto [before, after] : mapping)
                {
                    SCOPED_TRACE(before);
                    const auto result
                        = loadPackage<TypeParam>(savePackage(makePackage<TypeParam>({ 82, before }), format), &mapping);
                    EXPECT_EQ(result.mTargetActor, (RefNum{ 82, after }));
                }
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, unchangedOrderPreservesSerializedPackage)
        {
            const std::map<int, int> mapping{ { 100, 100 } };
            for (const auto format : modernFormats)
            {
                const auto data = savePackage(makePackage<TypeParam>({ 82, 100 }), format);
                EXPECT_EQ(savePackage(loadPackage<TypeParam>(data, &mapping), format), data);
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, absentMappingPreservesSerializedPackage)
        {
            for (const auto format : modernFormats)
            {
                const auto data = savePackage(makePackage<TypeParam>({ 82, 100 }), format);
                EXPECT_EQ(savePackage(loadPackage<TypeParam>(data, nullptr), format), data);
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, missingProviderClearsCachedTargetWithoutUsingReoccupiedSlot)
        {
            // Slot 100 now belongs to saved provider 101, while the old provider 100 is absent.
            const std::map<int, int> mapping{ { 101, 100 } };
            for (const auto format : modernFormats)
            {
                SCOPED_TRACE(format);
                const auto result
                    = loadPackage<TypeParam>(savePackage(makePackage<TypeParam>({ 82, 100 }), format), &mapping);
                EXPECT_FALSE(result.mTargetActor.isSet());
                EXPECT_EQ(savePackage(result, format), savePackage(makePackage<TypeParam>({}), format));
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, emptyMappingClearsContentTarget)
        {
            const std::map<int, int> mapping;
            for (const auto format : modernFormats)
            {
                const auto result
                    = loadPackage<TypeParam>(savePackage(makePackage<TypeParam>({ 82, 100 }), format), &mapping);
                EXPECT_FALSE(result.mTargetActor.isSet());
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, generatedTargetSurvivesEmptyMapping)
        {
            const std::map<int, int> mapping;
            for (const auto format : modernFormats)
            {
                const auto data = savePackage(makePackage<TypeParam>({ 9821, -1 }), format);
                EXPECT_EQ(savePackage(loadPackage<TypeParam>(data, &mapping), format), data);
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, unsetTargetStaysUnset)
        {
            const std::map<int, int> mapping{ { 0, 9 } };
            for (const auto format : modernFormats)
            {
                const auto data = savePackage(makePackage<TypeParam>({}), format);
                const auto result = loadPackage<TypeParam>(data, &mapping);
                EXPECT_FALSE(result.mTargetActor.isSet());
                EXPECT_EQ(savePackage(result, format), data);
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, zeroReferenceIndexStillRemapsItsContentFile)
        {
            const std::map<int, int> mapping{ { 0, 9 }, { 100, 101 } };
            for (const auto format : modernFormats)
            {
                for (const auto [before, after] : mapping)
                {
                    const auto result
                        = loadPackage<TypeParam>(savePackage(makePackage<TypeParam>({ 0, before }), format), &mapping);
                    EXPECT_EQ(result.mTargetActor, (RefNum{ 0, after }));
                    EXPECT_TRUE(result.mTargetActor.isSet());
                }
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, omittedModernTargetClearsPreviousValue)
        {
            const std::map<int, int> mapping{ { 100, 101 } };
            for (const auto format : modernFormats)
            {
                const auto data = savePackage(makePackage<TypeParam>({ 82, 100 }), format, TargetEncoding::Omitted);
                const auto result = loadPackage<TypeParam>(data, &mapping, { 999, 100 });
                EXPECT_FALSE(result.mTargetActor.isSet());
                EXPECT_EQ(savePackage(result, format), savePackage(makePackage<TypeParam>({}), format));
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, savedRemappedTargetSurvivesReloadAndAnotherReorder)
        {
            const std::map<int, int> firstMapping{ { 100, 101 } };
            const std::map<int, int> sameOrder{ { 101, 101 } };
            const std::map<int, int> secondMapping{ { 101, 4 } };
            for (const auto format : modernFormats)
            {
                SCOPED_TRACE(format);
                const auto original = savePackage(makePackage<TypeParam>({ 82, 100 }), format);
                const auto first = loadPackage<TypeParam>(original, &firstMapping);
                const auto saved = savePackage(first, format);
                const auto reloaded = loadPackage<TypeParam>(saved, &sameOrder);
                EXPECT_EQ(reloaded.mTargetActor, (RefNum{ 82, 101 }));
                EXPECT_EQ(savePackage(reloaded, format), saved);
                const auto reordered = loadPackage<TypeParam>(saved, &secondMapping);
                EXPECT_EQ(reordered.mTargetActor, (RefNum{ 82, 4 }));
                EXPECT_EQ(savePackage(reordered, format), savePackage(makePackage<TypeParam>({ 82, 4 }), format));
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, legacyActorIdIsNotInterpretedAsAContentReference)
        {
            const std::map<int, int> mapping{ { 0, 9 }, { 100, 101 } };
            for (const std::uint32_t actorId : { 0u, 82u, std::numeric_limits<std::uint32_t>::max() })
            {
                SCOPED_TRACE(actorId);
                const auto data = savePackage(
                    makePackage<TypeParam>({ actorId, -1 }), MaxActorIdSaveGameFormatVersion, TargetEncoding::Legacy);
                const auto result = loadPackage<TypeParam>(data, &mapping);
                EXPECT_EQ(result.mTargetActor, (RefNum{ actorId, -1 }));
            }
        }

        TYPED_TEST(Esm3AiTargetMappingTest, omittedLegacyActorIdKeepsLegacySentinel)
        {
            const std::map<int, int> mapping;
            const auto data
                = savePackage(makePackage<TypeParam>({}), MaxActorIdSaveGameFormatVersion, TargetEncoding::Omitted);
            const auto result = loadPackage<TypeParam>(data, &mapping);
            EXPECT_EQ(result.mTargetActor, (RefNum{ std::numeric_limits<std::uint32_t>::max(), -1 }));
        }

        void fillMixedSequence(AiSequence::AiSequence& sequence, int contentIndex)
        {
            using namespace AiSequence;
            sequence.mPackages.push_back(
                { Ai_Follow, std::make_unique<AiFollow>(makePackage<AiFollow>({ 82, contentIndex })) });
            auto travel = std::make_unique<AiTravel>();
            travel->mData = { -120.f, 40.f, 8.f };
            travel->mHidden = true;
            travel->mRepeat = false;
            sequence.mPackages.push_back({ Ai_Travel, std::move(travel) });
            sequence.mPackages.push_back(
                { Ai_Combat, std::make_unique<AiCombat>(makePackage<AiCombat>({ 83, contentIndex })) });
            sequence.mPackages.push_back(
                { Ai_Escort, std::make_unique<AiEscort>(makePackage<AiEscort>({ 84, contentIndex })) });
            sequence.mPackages.push_back(
                { Ai_Pursue, std::make_unique<AiPursue>(makePackage<AiPursue>({ 85, contentIndex })) });
            auto activate = std::make_unique<AiActivate>();
            activate->mTargetId = RefId::stringRefId("unrelated_activation_target");
            activate->mRepeat = true;
            sequence.mPackages.push_back({ Ai_Activate, std::move(activate) });
            sequence.mLastAiPackage = 2;
        }

        std::string saveSequence(const AiSequence::AiSequence& sequence, FormatVersion format)
        {
            std::stringstream stream;
            ESMWriter writer;
            writer.setFormatVersion(format);
            writer.save(stream);
            writer.startRecord(recordType);
            sequence.save(writer);
            writer.writeHNT("NEXT", sentinel);
            writer.endRecord(recordType);
            writer.close();
            return stream.str();
        }

        void loadSequence(const std::string& data, const std::map<int, int>& mapping, AiSequence::AiSequence& result)
        {
            ESMReader reader;
            reader.open(std::make_unique<std::stringstream>(data), "Mixed AI sequence fixture");
            reader.setContentFileMapping(&mapping);
            ASSERT_EQ(reader.getRecName().toInt(), recordType);
            reader.getRecHeader();
            result.load(reader);
            std::uint32_t next = 0;
            reader.getHNT(next, "NEXT");
            EXPECT_EQ(next, sentinel);
            EXPECT_FALSE(reader.hasMoreSubs());
            EXPECT_FALSE(reader.hasMoreRecs());
        }

        TEST(Esm3AiSequenceTargetMappingTest, mixedQueueRetainsOrderAndFieldsAcrossReorderedReload)
        {
            const std::array<std::map<int, int>, 3> mappings{ { { { 100, 101 } }, { { 101, 101 } }, { { 101, 4 } } } };
            const std::array contentIndices{ 101, 101, 4 };
            for (const auto format : modernFormats)
            {
                SCOPED_TRACE(format);
                AiSequence::AiSequence original;
                fillMixedSequence(original, 100);
                auto data = saveSequence(original, format);
                for (std::size_t i = 0; i < mappings.size(); ++i)
                {
                    SCOPED_TRACE(i);
                    AiSequence::AiSequence actual;
                    loadSequence(data, mappings[i], actual);
                    ASSERT_EQ(actual.mPackages.size(), 6);
                    EXPECT_EQ(actual.mLastAiPackage, 2);
                    AiSequence::AiSequence expected;
                    fillMixedSequence(expected, contentIndices[i]);
                    data = saveSequence(actual, format);
                    EXPECT_EQ(data, saveSequence(expected, format));
                }
            }
        }
    }
}
