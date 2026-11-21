#include "clone.hpp"

#include <set>

#include <osg/StateSet>

#include <osgAnimation/MorphGeometry>
#include <osgAnimation/RigGeometry>

#include <osgParticle/Emitter>
#include <osgParticle/ParticleProcessor>
#include <osgParticle/ParticleSystemUpdater>

#include <components/sceneutil/controller.hpp>
#include <components/sceneutil/morphgeometry.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/riggeometryosgaextension.hpp>
#include <components/sceneutil/scenefootprint.hpp>
#include <components/sceneutil/templateref.hpp>

namespace SceneUtil
{

    std::optional<std::size_t> estimateInstanceBytes(const osg::Node& root)
    {
        class Estimate : public osg::NodeVisitor
        {
        public:
            Estimate()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
                setNodeMaskOverride(~0u);
            }
            void apply(osg::Node& node) override
            {
                mBytes += 1024;
                traverse(node);
            }
            void geometry(const osg::Geometry& value)
            {
                mBytes += 1024;
                if (value.getVertexArray())
                    mBytes += 3 * std::size_t(value.getVertexArray()->getTotalDataSize());
                if (value.getNormalArray())
                    mBytes += 3 * std::size_t(value.getNormalArray()->getTotalDataSize());
                if (value.getColorArray())
                    mBytes += 3 * std::size_t(value.getColorArray()->getTotalDataSize());
                for (const auto& array : value.getTexCoordArrayList())
                    if (array)
                        mBytes += 3 * std::size_t(array->getTotalDataSize());
                for (const auto& array : value.getVertexAttribArrayList())
                    if (array)
                        mBytes += 3 * std::size_t(array->getTotalDataSize());
                for (const auto& primitive : value.getPrimitiveSetList())
                    mBytes += primitive->getTotalDataSize();
            }
            void apply(osg::Geometry& value) override
            {
                if (dynamic_cast<osgAnimation::RigGeometry*>(&value)
                    || dynamic_cast<osgAnimation::MorphGeometry*>(&value))
                    mSupported = false;
                else
                    geometry(value);
            }
            void apply(osg::Drawable& value) override
            {
                if (const auto* rig = dynamic_cast<const RigGeometry*>(&value))
                {
                    const auto source = rig->getSourceGeometry();
                    if (!source || !source->getVertexArray())
                    {
                        mSupported = false;
                        return;
                    }
                    geometry(*source);
                    // Private GPU indices/weights, double-buffered palettes and clone metadata.
                    // Source influence tables are immutable and shared by CopyOp.
                    mBytes += 64 * std::size_t(source->getVertexArray()->getNumElements()) + 65536;
                }
                else if (const auto* morph = dynamic_cast<const MorphGeometry*>(&value))
                {
                    const auto source = morph->getSourceGeometry();
                    if (!source)
                    {
                        mSupported = false;
                        return;
                    }
                    geometry(*source);
                    for (const auto& target : morph->getMorphTargetList())
                        if (target.getOffsets())
                            mBytes += 2 * std::size_t(target.getOffsets()->getTotalDataSize()) + 128;
                }
                else
                    mSupported = false; // Includes particles and third-party dynamic drawable implementations.
            }
            std::size_t mBytes = 0;
            bool mSupported = true;
        } estimate;
        const_cast<osg::Node&>(root).accept(estimate);
        if (!estimate.mSupported)
            return {};
        return estimate.mBytes;
    }

    std::optional<std::size_t> estimateRetainedSceneBytes(const osg::Node& root)
    {
        // Each source root is charged once, even when many derived nodes hold the
        // same TemplateRef. Internal drawable geometry also carries texture state.
        class Sources : public osg::NodeVisitor
        {
        public:
            std::set<const osg::Node*> mSeen;
            std::vector<const osg::Node*> mPending;
            SceneFootprint mFootprint;
            Sources() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) { setNodeMaskOverride(~0u); }
            void apply(osg::Node& node) override
            {
                if (!mSeen.insert(&node).second)
                    return;
                if (const auto* data = node.getUserDataContainer())
                    for (unsigned i = 0; i < data->getNumUserObjects(); ++i)
                        if (const auto* ref = dynamic_cast<const TemplateRef*>(data->getUserObject(i)))
                            if (const auto* source = dynamic_cast<const osg::Node*>(ref->getTemplate()))
                                mPending.push_back(source);
                if (const auto* rig = dynamic_cast<const RigGeometry*>(&node))
                {
                    if (const auto source = rig->getSourceGeometry())
                        mPending.push_back(source.get());
                }
                else if (const auto* morph = dynamic_cast<const MorphGeometry*>(&node))
                {
                    if (const auto source = morph->getSourceGeometry())
                        mPending.push_back(source.get());
                }
                traverse(node);
            }
        } sources;
        sources.mPending.push_back(&root);
        std::set<const osg::Node*> roots;
        std::size_t bytes = 0;
        while (!sources.mPending.empty())
        {
            const auto* node = sources.mPending.back();
            sources.mPending.pop_back();
            if (!roots.insert(node).second)
                continue;
            const auto instance = estimateInstanceBytes(*node);
            if (!instance)
                return {};
            bytes += *instance + sources.mFootprint.measure(*const_cast<osg::Node*>(node));
            const_cast<osg::Node*>(node)->accept(sources);
        }
        return bytes;
    }

    CopyOp::CopyOp()
    {
        setCopyFlags(osg::CopyOp::DEEP_COPY_NODES
            // Controller might need different inputs per scene instance
            | osg::CopyOp::DEEP_COPY_CALLBACKS | osg::CopyOp::DEEP_COPY_USERDATA);
    }

    osg::Node* CopyOp::operator()(const osg::Node* node) const
    {
        if (const osgParticle::ParticleProcessor* processor = dynamic_cast<const osgParticle::ParticleProcessor*>(node))
            return operator()(processor);
        if (const osgParticle::ParticleSystemUpdater* updater
            = dynamic_cast<const osgParticle::ParticleSystemUpdater*>(node))
        {
            osgParticle::ParticleSystemUpdater* cloned
                = new osgParticle::ParticleSystemUpdater(*updater, osg::CopyOp::SHALLOW_COPY);
            mUpdaterToOldPs[cloned] = updater->getParticleSystem(0);
            return cloned;
        }

        osg::Node* cloned = osg::CopyOp::operator()(node);
        if (cloned)
            mClonedNodes[node] = cloned;

        return cloned;
    }

    osg::Callback* CopyOp::operator()(const osg::Callback* callback) const
    {
        osg::Callback* cloned = osg::CopyOp::operator()(callback);
        for (osg::Callback* cb = cloned; cb != nullptr; cb = cb->getNestedCallback())
        {
            if (auto* ctrl = dynamic_cast<Controller*>(cb))
                mControllersToRemap.push_back(ctrl);
        }
        return cloned;
    }

    osg::Drawable* CopyOp::operator()(const osg::Drawable* drawable) const
    {
        if (const osgParticle::ParticleSystem* partsys = dynamic_cast<const osgParticle::ParticleSystem*>(drawable))
            return operator()(partsys);

        if (dynamic_cast<const SceneUtil::RigGeometry*>(drawable)
            || dynamic_cast<const SceneUtil::MorphGeometry*>(drawable)
            || dynamic_cast<const osgAnimation::RigGeometry*>(drawable)
            || dynamic_cast<const osgAnimation::MorphGeometry*>(drawable)
            || dynamic_cast<const SceneUtil::RigGeometryHolder*>(drawable))
        {
            return static_cast<osg::Drawable*>(drawable->clone(*this));
        }

        return osg::CopyOp::operator()(drawable);
    }

    osgParticle::ParticleProcessor* CopyOp::operator()(const osgParticle::ParticleProcessor* processor) const
    {
        osgParticle::ParticleProcessor* cloned
            = static_cast<osgParticle::ParticleProcessor*>(processor->clone(osg::CopyOp::DEEP_COPY_CALLBACKS));
        for (const auto& oldPsNewPsPair : mOldPsToNewPs)
        {
            if (processor->getParticleSystem() == oldPsNewPsPair.first)
            {
                cloned->setParticleSystem(oldPsNewPsPair.second);
                return cloned;
            }
        }

        mProcessorToOldPs[cloned] = processor->getParticleSystem();
        return cloned;
    }

    osgParticle::ParticleSystem* CopyOp::operator()(const osgParticle::ParticleSystem* partsys) const
    {
        osgParticle::ParticleSystem* cloned = static_cast<osgParticle::ParticleSystem*>(partsys->clone(*this));

        for (const auto& processorPsPair : mProcessorToOldPs)
        {
            if (processorPsPair.second == partsys)
            {
                processorPsPair.first->setParticleSystem(cloned);
            }
        }
        for (const auto& updaterPsPair : mUpdaterToOldPs)
        {
            if (updaterPsPair.second == partsys)
            {
                osgParticle::ParticleSystemUpdater* updater = updaterPsPair.first;
                updater->removeParticleSystem(updater->getParticleSystem(0));
                updater->addParticleSystem(cloned);
            }
        }
        // In rare situations a particle processor may be placed after the particle system in the scene graph.
        mOldPsToNewPs[partsys] = cloned;

        return cloned;
    }

    osg::Node* CopyOp::getClonedNode(const osg::Node* original) const
    {
        auto it = mClonedNodes.find(original);
        return it != mClonedNodes.end() ? it->second : nullptr;
    }

    void CopyOp::remapControllerTargets() const
    {
        for (Controller* ctrl : mControllersToRemap)
            ctrl->remapTargets(*this);
    }

}
