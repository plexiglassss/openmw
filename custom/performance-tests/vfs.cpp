#include <cassert>
#include <components/misc/parallelfor.hpp>
#include <components/vfs/filesystemarchive.hpp>
#include <components/vfs/manager.hpp>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path root(argv[1]);
    assert(!std::filesystem::exists(root));
    std::filesystem::create_directories(root);
    std::vector<std::filesystem::path> dirs;
    for (int i = 0; i < 12; ++i)
    {
        dirs.push_back(root / std::to_string(i));
        std::filesystem::create_directories(dirs.back() / "Textures");
        std::ofstream(dirs.back() / "Textures" / "Shared.DDS") << i;
        std::ofstream(dirs.back() / (std::to_string(i) + ".nif")) << i;
    }
    // A directory symlink and Unicode names must retain the serial scanner's semantics.
    std::filesystem::create_directory_symlink(dirs.front() / "Textures", dirs.back() / "linked");
    std::ofstream(dirs.back() / std::filesystem::path(u8"é.nif")) << "unicode";
    VFS::Manager serial, parallel;
    for (auto& dir : dirs)
        serial.addArchive(std::make_unique<VFS::FileSystemArchive>(dir));
    serial.buildIndex();
    std::vector<std::unique_ptr<VFS::FileSystemArchive>> results(dirs.size());
    Misc::parallelFor(
        dirs.size(), 4, [&](std::size_t i) { results[i] = std::make_unique<VFS::FileSystemArchive>(dirs[i]); });
    for (auto& result : results)
        parallel.addArchive(std::move(result));
    parallel.buildIndex();
    for (const auto& name : { "textures/shared.dds", "linked/shared.dds", "0.nif", "11.nif", "é.nif" })
    {
        const VFS::Path::Normalized path(name);
        assert(serial.getArchive(path) == parallel.getArchive(path));
        auto a = serial.get(path), b = parallel.get(path);
        std::string av, bv;
        *a >> av;
        *b >> bv;
        assert(av == bv);
        if (std::string(name) == "textures/shared.dds")
            assert(av == "11");
    }
    bool failed = false;
    try
    {
        Misc::parallelFor(
            2, 4, [&](std::size_t i) { VFS::FileSystemArchive missing(root / ("missing" + std::to_string(i))); });
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        failed = true;
        assert(std::string(e.what()).find("missing0") != std::string::npos);
    }
    assert(failed);
    std::filesystem::remove_all(root);
    std::cout << "VFS serial/parallel winners, file contents, symlinks, Unicode and failures passed\n";
}
