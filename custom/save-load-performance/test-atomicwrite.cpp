#include <components/files/atomicwrite.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#endif

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    std::string read(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        require(bool(stream), "Read failed");
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }
}

int main()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("openmw-atomic-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    try
    {
        const auto destination = root / "save.omwsave";
        Files::writeFileAtomically(destination, {});
        require(read(destination).empty(), "Empty save differs");
        std::string bytes(3 * 1024 * 1024 + 19, '\0');
        for (std::size_t i = 0; i != bytes.size(); ++i)
            bytes[i] = static_cast<char>(i % 251);
        Files::writeFileAtomically(destination, bytes);
        require(read(destination) == bytes, "Binary/chunked save differs");
        Files::writeFileAtomically(destination, "previous save");
        require(read(destination) == "previous save", "Replacement differs");

        // An actual operating-system write/publish failure must leave the
        // previously saved file intact and remove the unpublished sibling.
        bool failed = false;
#ifdef _WIN32
        HANDLE locked = CreateFileW(
            destination.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "Could not lock previous save");
        try
        {
            Files::writeFileAtomically(destination, bytes);
        }
        catch (const std::system_error&)
        {
            failed = true;
        }
        CloseHandle(locked);
#else
        struct rlimit previous{};
        require(getrlimit(RLIMIT_FSIZE, &previous) == 0, "getrlimit failed");
        struct rlimit limited = previous;
        limited.rlim_cur = 1024;
        const auto oldSignal = std::signal(SIGXFSZ, SIG_IGN);
        require(setrlimit(RLIMIT_FSIZE, &limited) == 0, "setrlimit failed");
        try
        {
            Files::writeFileAtomically(destination, bytes);
        }
        catch (const std::system_error&)
        {
            failed = true;
        }
        require(setrlimit(RLIMIT_FSIZE, &previous) == 0, "restore rlimit failed");
        std::signal(SIGXFSZ, oldSignal);
#endif
        require(failed, "Failure injection did not reach the save writer");
        require(read(destination) == "previous save", "Failure damaged the old save");
        require(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}) == 1,
            "Temporary save leaked");

        const auto blocked = root / "blocked.omwsave";
        std::filesystem::create_directory(blocked);
        Files::writeFileAtomically(blocked / "keep", "keep");
        failed = false;
        try
        {
            Files::writeFileAtomically(blocked, bytes);
        }
        catch (const std::system_error&)
        {
            failed = true;
        }
        require(failed, "Publishing onto a nonempty directory should fail");
        require(read(blocked / "keep") == "keep", "Publish failure damaged destination");
        require(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}) == 2,
            "Publish failure leaked a temporary");
        Files::writeFileAtomically(destination, bytes);
        require(read(destination) == bytes, "Successful retry differs");
        std::filesystem::remove_all(root);
        std::cout << "PASS: actual atomic writer; empty, binary, chunked, replacement, OS failure, cleanup, retry\n";
    }
    catch (...)
    {
        std::filesystem::remove_all(root);
        throw;
    }
}
