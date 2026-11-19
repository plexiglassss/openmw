#include "niffilemanager.hpp"

#include <iostream>

#include <components/vfs/manager.hpp>

#include "objectcache.hpp"

namespace Resource
{

    NifFileManager::NifFileManager(const VFS::Manager* vfs, const ToUTF8::StatelessUtf8Encoder* encoder)
        // NIF files aren't needed any more once the converted objects are cached in SceneManager / BulletShapeManager,
        // so no point in using an expiry delay.
        : ResourceManager(vfs, 0)
        , mEncoder(encoder)
    {
    }

    NifFileManager::~NifFileManager() = default;

    Nif::NIFFilePtr NifFileManager::get(VFS::Path::NormalizedView name, Nif::NIFFilePtr prepared)
    {
        if (Nif::NIFFilePtr cached = mCache->getRefFromObjectCache(name))
            return cached;

        auto file = prepared ? std::move(prepared) : read(name);
        mCache->addEntryToObjectCache(name.value(), file);
        return file;
    }

    bool NifFileManager::isCached(VFS::Path::NormalizedView name) const
    {
        return mCache->getRefFromObjectCache(name) != nullptr;
    }

    Nif::NIFFilePtr NifFileManager::read(VFS::Path::NormalizedView name)
    {
        return mPendingReads.get(std::string(name.value()), [this, name] {
            auto file = std::make_shared<Nif::NIFFile>(name);
            Nif::Reader reader(*file, mEncoder);
            reader.parse(mVFS->get(name));
            return file;
        }, [](const auto& ready) { return ready.get(); });
    }

    void NifFileManager::reportStats(unsigned int frameNumber, osg::Stats* stats) const
    {
        Resource::reportStats("Nif", frameNumber, mCache->getStats(), *stats);
    }

}
