#ifndef OPENMW_FILES_ATOMICWRITE_H
#define OPENMW_FILES_ATOMICWRITE_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <components/misc/windows.hpp>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Files
{
    inline bool isAtomicWriteTemporary(const std::filesystem::path& path)
    {
        const auto extension = path.extension().string();
        constexpr std::string_view prefix = ".writing-";
        if (!extension.starts_with(prefix))
            return false;
        const std::string_view suffix(extension.data() + prefix.size(), extension.size() - prefix.size());
        const auto separator = suffix.find('-');
        const auto digits = [](std::string_view value) {
            return !value.empty()
                && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
        };
        return separator != std::string_view::npos && digits(suffix.substr(0, separator))
            && digits(suffix.substr(separator + 1));
    }

    // The worker owns the byte snapshot. Never truncate the old destination:
    // publish a complete sibling file only after its data has been flushed.
    inline void writeFileAtomically(const std::filesystem::path& destination, std::string_view bytes)
    {
        static std::atomic_uint64_t sequence{ 0 };
        std::filesystem::path temporary;
#ifdef _WIN32
        HANDLE file = INVALID_HANDLE_VALUE;
#else
        int file = -1;
#endif
        for (int attempt = 0; attempt != 128; ++attempt)
        {
            temporary = destination;
            temporary += ".writing-"
                + std::to_string(
                    static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()))
                + "-" + std::to_string(sequence.fetch_add(1));
#ifdef _WIN32
            file
                = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE)
                break;
            const auto error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
                throw std::system_error(error, std::system_category(), "Creating save temporary file");
#else
            file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (file != -1)
                break;
            if (errno != EEXIST)
                throw std::system_error(errno, std::generic_category(), "Creating save temporary file");
#endif
        }
#ifdef _WIN32
        if (file == INVALID_HANDLE_VALUE)
#else
        if (file == -1)
#endif
            throw std::runtime_error("Could not allocate save temporary file");
        try
        {
            while (!bytes.empty())
            {
                const auto chunk = std::min<std::size_t>(bytes.size(), 1024 * 1024);
#ifdef _WIN32
                DWORD written = 0;
                if (!WriteFile(file, bytes.data(), static_cast<DWORD>(chunk), &written, nullptr) || written == 0)
                    throw std::system_error(GetLastError(), std::system_category(), "Writing save");
#else
                const auto written = ::write(file, bytes.data(), chunk);
                if (written == -1 && errno == EINTR)
                    continue;
                if (written <= 0)
                    throw std::system_error(errno, std::generic_category(), "Writing save");
#endif
                bytes.remove_prefix(static_cast<std::size_t>(written));
            }
#ifdef _WIN32
            if (!FlushFileBuffers(file))
                throw std::system_error(GetLastError(), std::system_category(), "Flushing save");
            const bool closed = CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            if (!closed)
                throw std::system_error(GetLastError(), std::system_category(), "Closing save");
            if (!MoveFileExW(
                    temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::system_error(GetLastError(), std::system_category(), "Publishing save");
#else
            if (::fsync(file) != 0)
                throw std::system_error(errno, std::generic_category(), "Flushing save");
            const auto closed = ::close(file);
            file = -1;
            if (closed != 0)
                throw std::system_error(errno, std::generic_category(), "Closing save");
            std::filesystem::rename(temporary, destination);
#endif
        }
        catch (...)
        {
#ifdef _WIN32
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
#else
            if (file != -1)
                ::close(file);
#endif
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
    }
}

#endif
