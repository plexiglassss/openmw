#include <apps/openmw/mwstate/character.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/files/atomicwrite.hpp>

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <sstream>

namespace
{
    TEST(SavePublication, InterruptedSiblingIsNotOfferedAsAValidSave)
    {
        const auto root = std::filesystem::temp_directory_path()
            / ("openmw-slot-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        struct Cleanup
        {
            std::filesystem::path path;
            ~Cleanup()
            {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{ root };
        ESM::SavedGame profile{};
        profile.mPlayerName = "Test player";
        profile.mContentFiles = { "Morrowind.esm" };
        profile.mDescription = "Previous valid save";
        std::ostringstream stream;
        ESM::ESMWriter writer;
        writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
        writer.save(stream);
        writer.startRecord(ESM::REC_SAVE);
        profile.save(writer);
        writer.endRecord(ESM::REC_SAVE);
        writer.close();
        Files::writeFileAtomically(root / "previous.omwsave", stream.str());
        // This header is readable, but represents an interrupted unpublished save.
        Files::writeFileAtomically(root / "previous.omwsave.writing-123456-7", stream.str());
        MWState::Character character(root, "Morrowind.esm");
        ASSERT_EQ(std::distance(character.begin(), character.end()), 1);
        EXPECT_EQ(character.begin()->mPath.filename(), "previous.omwsave");
    }
}
