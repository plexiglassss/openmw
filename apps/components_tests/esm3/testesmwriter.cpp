#include <components/esm3/esmwriter.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <random>

namespace ESM
{
    namespace
    {
        using namespace ::testing;

        struct Esm3EsmWriterTest : public Test
        {
            std::minstd_rand mRandom;
            std::uniform_int_distribution<short> mRefIdDistribution{ 'a', 'z' };

            std::string generateRandomString(std::size_t size)
            {
                std::string result;
                std::generate_n(
                    std::back_inserter(result), size, [&] { return static_cast<char>(mRefIdDistribution(mRandom)); });
                return result;
            }
        };

        TEST_F(Esm3EsmWriterTest, saveShouldThrowExceptionOnWhenTruncatingHeaderStrings)
        {
            const std::string author = generateRandomString(33);
            const std::string description = generateRandomString(257);

            std::stringstream stream;

            ESMWriter writer;
            writer.setAuthor(author);
            writer.setDescription(description);
            writer.setFormatVersion(MaxLimitedSizeStringsFormatVersion);
            EXPECT_THROW(writer.save(stream), std::runtime_error);
        }

        TEST_F(Esm3EsmWriterTest, writeFixedStringShouldThrowExceptionOnTruncate)
        {
            std::stringstream stream;

            ESMWriter writer;
            writer.setFormatVersion(MaxLimitedSizeStringsFormatVersion);
            writer.save(stream);
            EXPECT_THROW(writer.writeMaybeFixedSizeString(generateRandomString(33), 32), std::runtime_error);
        }

        TEST_F(Esm3EsmWriterTest, encodedRecordsMatchOrdinarySerializationAndRecordCounts)
        {
            const auto record = [](ESMWriter& writer) {
                writer.startRecord(REC_CSTA);
                writer.writeCellId(RefId::stringRefId("a saved cell"));
                writer.writeHNT("WLVL", 12.5f);
                writer.writeHNT("HFOW", 0);
                writer.endRecord(REC_CSTA);
            };
            std::ostringstream fragment;
            ESMWriter encoder;
            encoder.setFormatVersion(CurrentSaveGameFormatVersion);
            encoder.save(fragment, false);
            record(encoder);
            encoder.close();
            EXPECT_EQ(encoder.getRecordCount(), 1);
            std::ostringstream normal, reused;
            ESMWriter a, b;
            a.setFormatVersion(CurrentSaveGameFormatVersion);
            b.setFormatVersion(CurrentSaveGameFormatVersion);
            a.save(normal);
            b.save(reused);
            for (int i = 0; i != 64; ++i)
            {
                record(a);
                b.writeEncodedRecord(fragment.str());
            }
            a.close();
            b.close();
            EXPECT_EQ(normal.str(), reused.str());
            EXPECT_EQ(a.getRecordCount(), b.getRecordCount());
        }

        TEST_F(Esm3EsmWriterTest, encodedRecordsRejectTruncationAndNestedUse)
        {
            std::ostringstream stream;
            ESMWriter writer;
            writer.save(stream, false);
            EXPECT_THROW(writer.writeEncodedRecord("short"), std::logic_error);
            EXPECT_THROW(writer.writeEncodedRecord(std::string(17, '\0')), std::logic_error);
            writer.startRecord(REC_CSTA);
            EXPECT_THROW(writer.writeEncodedRecord(std::string(16, '\0')), std::logic_error);
            writer.endRecord(REC_CSTA);
            writer.close();
        }

        struct Esm3EsmWriterRefIdSizeTest : TestWithParam<std::pair<RefId, std::size_t>>
        {
        };

        // If this test failed probably there is a change in RefId format and CurrentSaveGameFormatVersion should be
        // incremented, current version should be handled.
        TEST_P(Esm3EsmWriterRefIdSizeTest, writeHRefIdShouldProduceCertainNumberOfBytes)
        {
            const auto [refId, size] = GetParam();

            std::ostringstream stream;

            {
                ESMWriter writer;
                writer.setFormatVersion(CurrentSaveGameFormatVersion);
                writer.save(stream);
                writer.writeHRefId(refId);
            }

            EXPECT_EQ(stream.str().size(), size);
        }

        const std::vector<std::pair<RefId, std::size_t>> refIdSizes = {
            { RefId(), 57 },
            { RefId::stringRefId(std::string(32, 'a')), 89 },
            { RefId::formIdRefId({ 0x1f, 0 }), 65 },
            { RefId::generated(0x1f), 65 },
            { RefId::index(REC_INGR, 0x1f), 65 },
            { RefId::esm3ExteriorCell(-42, 42), 65 },
        };

        INSTANTIATE_TEST_SUITE_P(RefIds, Esm3EsmWriterRefIdSizeTest, ValuesIn(refIdSizes));
    }
}
