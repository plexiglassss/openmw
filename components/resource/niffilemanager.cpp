#include "niffilemanager.hpp"

#include <iostream>

#include <osg/Object>

#include <components/vfs/manager.hpp>

#include "objectcache.hpp"

namespace Resource
{

    class NifFileHolder : public osg::Object
    {
    public:
        NifFileHolder(const Nif::NIFFilePtr& file)
            : mNifFile(file)
        {
        }
        NifFileHolder(const NifFileHolder& copy, const osg::CopyOp& copyop)
            : mNifFile(copy.mNifFile)
        {
        }

        NifFileHolder() = default;

        META_Object(Resource, NifFileHolder)

        Nif::NIFFilePtr mNifFile;
    };

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
        osg::ref_ptr<osg::Object> obj = mCache->getRefFromObjectCache(name);
        if (obj != nullptr)
            return static_cast<NifFileHolder*>(obj.get())->mNifFile;

        auto file = prepared ? std::move(prepared) : read(name);
        obj = new NifFileHolder(file);
        mCache->addEntryToObjectCache(name.value(), obj);
        return file;
    }

    bool NifFileManager::isCached(VFS::Path::NormalizedView name) const
    {
        return mCache->getRefFromObjectCache(name).valid();
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
