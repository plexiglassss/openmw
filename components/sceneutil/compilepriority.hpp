#ifndef OPENMW_COMPONENTS_SCENEUTIL_COMPILEPRIORITY_H
#define OPENMW_COMPONENTS_SCENEUTIL_COMPILEPRIORITY_H

#include <osg/NodeVisitor>
#include <osgUtil/IncrementalCompileOperation>

#include <unordered_set>

namespace SceneUtil
{
    // Capture dependencies before enqueue. OSG mutates its CompileLists on the
    // graphics thread without holding the queue mutex: never inspect live lists.
    class ResourceCompileSet final : public osgUtil::IncrementalCompileOperation::CompileSet
    {
    public:
        explicit ResourceCompileSet(osg::Node* node)
            : CompileSet(node) {}
        void captureResources();
        const std::unordered_set<const osg::Object*>& resources() const { return mResources; }

    private:
        std::unordered_set<const osg::Object*> mResources;
    };

    osg::ref_ptr<ResourceCompileSet> prepareCompileSet(osgUtil::IncrementalCompileOperation& operation,
        osg::Node* node, osgUtil::StateToCompile* state = nullptr);
    void enqueueCompile(osgUtil::IncrementalCompileOperation& operation, osg::Node* node);

    // Read-only collection; does not mark resources or add duplicate GL requests.
    class CompilePriorityVisitor : public osg::NodeVisitor
    {
    public:
        CompilePriorityVisitor();
        void apply(osg::Node& node) override;
        void apply(osg::Drawable& drawable) override;
        void collect(const osg::StateSet* state);
        std::size_t promote(osgUtil::IncrementalCompileOperation& operation) const;

    private:
        std::unordered_set<const osg::Object*> mResources;
    };
}
#endif
