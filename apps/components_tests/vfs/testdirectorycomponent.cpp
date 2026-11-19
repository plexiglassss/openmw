#include <components/vfs/archive.hpp>
#include <components/vfs/manager.hpp>

#include <gtest/gtest.h>

namespace
{
    class DirectoryFixtureArchive : public VFS::Archive
    {
    public:
        void listResources(VFS::FileMap& files) override
        {
            for (const char* path : { "nested/recipes/a", "recipes/b", "nested/recipes/sub/c", "xrecipes/no",
                     "nested/recipes2/no", "nested/recipes", "prefix/Recipes/D", "nested/recipes/recipes/a" })
                files[VFS::Path::Normalized(path)] = nullptr;
        }
        bool contains(VFS::Path::NormalizedView) const override { return false; }
        std::string getDescription() const override { return "directory fixture"; }
    };

    TEST(VfsDirectoryComponent, MatchesWholeDirectoriesAndKeepsWinningIndexOrder)
    {
        VFS::Manager vfs;
        vfs.addArchive(std::make_unique<DirectoryFixtureArchive>());
        vfs.addArchive(std::make_unique<DirectoryFixtureArchive>());
        vfs.buildIndex();
        const auto paths = vfs.getPathsInDirectory("ReCiPeS");
        const std::vector<std::string> expected{ "nested/recipes/a", "nested/recipes/recipes/a",
            "nested/recipes/sub/c", "prefix/recipes/d", "recipes/b" };
        EXPECT_EQ(paths, expected);
        EXPECT_TRUE(vfs.getPathsInDirectory("absent").empty());
        EXPECT_THROW(vfs.getPathsInDirectory("nested/recipes"), std::invalid_argument);
        EXPECT_THROW(vfs.getPathsInDirectory(""), std::invalid_argument);
        EXPECT_THROW(vfs.getPathsInDirectory("recipes/"), std::invalid_argument);
        vfs.reset();
        EXPECT_TRUE(vfs.getPathsInDirectory("recipes").empty());
        EXPECT_EQ(paths, expected);
    }
}
