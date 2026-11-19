#ifndef OPENMW_COMPONENTS_RESOURCE_IMAGEMANAGER_H
#define OPENMW_COMPONENTS_RESOURCE_IMAGEMANAGER_H

#include <osg/Image>
#include <osg/Texture2D>
#include <osg/ref_ptr>

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <components/vfs/pathutil.hpp>

#include "resourcemanager.hpp"

namespace osgDB
{
    class Options;
}

namespace Resource
{

    /// @brief Handles loading/caching of Images.
    /// @note May be used from any thread.
    class ImageManager : public ResourceManager
    {
    public:
        explicit ImageManager(const VFS::Manager* vfs, double expiryDelay);
        ~ImageManager();

        /// Create or retrieve an Image
        /// Returns the dummy image if the given image is not found.
        osg::ref_ptr<osg::Image> getImage(VFS::Path::NormalizedView path, bool disableFlip = false);
        /// Resolve an automatic material texture. Both hits and misses belong to the current VFS index.
        std::optional<VFS::Path::Normalized> findCompanion(std::string_view diffuse, std::string_view pattern);

        osg::Image* getWarningImage();

        void reportStats(unsigned int frameNumber, osg::Stats* stats) const override;

    private:
        osg::ref_ptr<osg::Image> mWarningImage;
        osg::ref_ptr<osgDB::Options> mOptions;
        struct CompanionEntry
        {
            std::string mDiffuse, mPattern;
            std::optional<VFS::Path::Normalized> mPath;
            std::uint64_t mRevision = 0;
        };
        struct CompanionBucket
        {
            std::mutex mMutex;
            std::array<CompanionEntry, 128> mEntries;
        };
        std::array<CompanionBucket, 16> mCompanions;

        ImageManager(const ImageManager&);
        void operator=(const ImageManager&);
    };

}

#endif
