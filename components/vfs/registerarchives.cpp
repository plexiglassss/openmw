#include "registerarchives.hpp"

#include <filesystem>
#include <thread>

#include <components/diagnostics/timing.hpp>
#include <components/misc/parallelfor.hpp>
#include <set>
#include <stdexcept>

#include <components/debug/debuglog.hpp>

#include <components/vfs/bsaarchive.hpp>
#include <components/vfs/filesystemarchive.hpp>
#include <components/vfs/manager.hpp>

namespace VFS
{

    void registerArchives(VFS::Manager* vfs, const Files::Collections& collections,
        const std::vector<std::string>& archives, bool useLooseFiles, const ToUTF8::StatelessUtf8Encoder* encoder)
    {
        Diagnostics::Scope timing("startup", "vfs", "registerArchives");
        const Files::PathContainer& dataDirs = collections.getPaths();

        for (std::vector<std::string>::const_iterator archive = archives.begin(); archive != archives.end(); ++archive)
        {
            if (collections.doesExist(*archive))
            {
                // Last BSA has the highest priority
                const auto archivePath = collections.getPath(*archive);
                Log(Debug::Info) << "Adding BSA archive " << archivePath;
                vfs->addArchive(makeBsaArchive(archivePath, encoder));
            }
            else
            {
                throw std::runtime_error("Archive '" + *archive + "' not found");
            }
        }

        if (useLooseFiles)
        {
            Diagnostics::Scope scanTiming("startup", "vfs", "scanDataDirectories");
            std::vector<std::filesystem::path> uniqueDirs;
            std::set<std::filesystem::path> seen;
            for (const auto& dataDir : dataDirs)
            {
                if (seen.insert(dataDir).second)
                {
                    Log(Debug::Info) << "Adding data directory " << dataDir;
                    uniqueDirs.push_back(dataDir);
                }
                else
                    Log(Debug::Info) << "Ignoring duplicate data directory " << dataDir;
            }
            std::vector<std::unique_ptr<FileSystemArchive>> scanned(uniqueDirs.size());
            // Bound disk pressure and memory use; never mutate the VFS from workers.
            const auto workers = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
            Misc::parallelFor(uniqueDirs.size(), workers,
                [&](std::size_t i) { scanned[i] = std::make_unique<FileSystemArchive>(uniqueDirs[i]); });
            // Completion order must not change which mod wins an asset conflict.
            for (auto& archive : scanned)
                vfs->addArchive(std::move(archive));
        }

        Diagnostics::Scope indexTiming("startup", "vfs", "buildIndex");
        vfs->buildIndex();
    }

}
