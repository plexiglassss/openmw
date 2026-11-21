#ifndef OPENMW_COMPONENTS_SCENEUTIL_CLONE_H
#define OPENMW_COMPONENTS_SCENEUTIL_CLONE_H

#include <map>
#include <optional>
#include <vector>

#include <osg/CopyOp>

namespace osgParticle
{
    class ParticleProcessor;
    class ParticleSystem;
    class ParticleSystemUpdater;
}

namespace SceneUtil
{
    class Controller;

    /// Conservative speculative-clone budget, including internal rig/morph arrays.
    /// Unknown dynamic drawables retain the ordinary demand path.
    std::optional<std::size_t> estimateInstanceBytes(const osg::Node& node);

    // Includes retained source templates, internal rig/morph arrays and a texture
    // allowance. Unknown drawable layouts are not admitted to additional caches.
    std::optional<std::size_t> estimateRetainedSceneBytes(const osg::Node& node);

    /// @par Defines the cloning behaviour we need:
    /// * Assigns updated ParticleSystem pointers on cloned emitters and programs.
    /// * Deep copies RigGeometry and MorphGeometry so they can animate without affecting clones.
    /// @warning Avoid using this class directly. The safety of cloning operations depends on the copy flags and the
    /// objects involved. Consider using SceneManager::cloneNode for additional safety.
    /// @warning Do not use an object of this class for more than one copy operation.
    class CopyOp : public osg::CopyOp
    {
    public:
        CopyOp();

        virtual osgParticle::ParticleSystem* operator()(const osgParticle::ParticleSystem* partsys) const;
        virtual osgParticle::ParticleProcessor* operator()(const osgParticle::ParticleProcessor* processor) const;

        osg::Node* operator()(const osg::Node* node) const override;
        osg::Drawable* operator()(const osg::Drawable* drawable) const override;
        osg::Callback* operator()(const osg::Callback* callback) const override;

        osg::Node* getClonedNode(const osg::Node* original) const;
        void remapControllerTargets() const;

    private:
        // maps new pointers to their old pointers
        // a little messy, but I think this should be the most efficient way
        mutable std::map<osgParticle::ParticleProcessor*, const osgParticle::ParticleSystem*> mProcessorToOldPs;
        mutable std::map<osgParticle::ParticleSystemUpdater*, const osgParticle::ParticleSystem*> mUpdaterToOldPs;
        mutable std::map<const osgParticle::ParticleSystem*, osgParticle::ParticleSystem*> mOldPsToNewPs;
        mutable std::map<const osg::Node*, osg::Node*> mClonedNodes;
        mutable std::vector<Controller*> mControllersToRemap;
    };

}

#endif
