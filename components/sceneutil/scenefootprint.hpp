#ifndef OPENMW_COMPONENTS_SCENEUTIL_SCENEFOOTPRINT_H
#define OPENMW_COMPONENTS_SCENEUTIL_SCENEFOOTPRINT_H

#include <osg/Geometry>
#include <osg/NodeVisitor>
#include <osg/Texture>
#include <unordered_set>

namespace SceneUtil
{
    // Admission estimate for immutable CPU graphs, not measured RAM or VRAM.
    // Applying a shared texture can clear its image slots on the graphics thread.
    // Never inspect those slots from a preload worker.
    class SceneFootprint : public osg::NodeVisitor
    {
        std::unordered_set<const osg::Object*> mSeen;
        std::size_t mBytes = 0;
        void buffer(const osg::BufferData* data)
        {
            if (data && mSeen.insert(data).second)
                mBytes += 128 + data->getTotalDataSize();
        }
        void state(const osg::StateSet* value)
        {
            if (!value || !mSeen.insert(value).second)
                return;
            mBytes += 512;
            for (unsigned unit = 0; unit < value->getTextureAttributeList().size(); ++unit)
                if (const auto* texture = dynamic_cast<const osg::Texture*>(
                        value->getTextureAttribute(unit, osg::StateAttribute::TEXTURE)))
                    if (mSeen.insert(texture).second)
                        mBytes += 256 + 4ull * 1024 * 1024;
        }
    public:
        SceneFootprint() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) { setNodeMaskOverride(~0u); }
        void apply(osg::Node& node) override
        {
            if (!mSeen.insert(&node).second)
                return;
            mBytes += 512;
            state(node.getStateSet());
            if (auto* geometry = node.asGeometry())
            {
                osg::Geometry::ArrayList arrays;
                geometry->getArrayList(arrays);
                for (const auto& array : arrays)
                    buffer(array);
                for (unsigned i = 0; i < geometry->getNumPrimitiveSets(); ++i)
                    buffer(geometry->getPrimitiveSet(i));
            }
            traverse(node);
        }
        std::size_t measure(osg::Node& node)
        {
            const auto before = mBytes;
            node.accept(*this);
            return mBytes - before;
        }
    };
}
#endif
