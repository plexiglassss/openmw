#ifndef OPENMW_COMPONENTS_SCENEUTIL_COMPILEPRIORITY_H
#define OPENMW_COMPONENTS_SCENEUTIL_COMPILEPRIORITY_H

#include <osg/NodeVisitor>
#include <osg/observer_ptr>
#include <osgUtil/IncrementalCompileOperation>

#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <cstdint>
#include <memory>

namespace SceneUtil
{
    // Opt-in contract for Program subclasses that retain osg::Program's shader
    // compilation semantics. Unknown custom compile overrides stay indivisible.
    class SplitShaderCompilation
    {
    public:
        virtual ~SplitShaderCompilation() = default;
    };

    // Capture dependencies before enqueue. OSG mutates its CompileLists on the
    // graphics thread without holding the queue mutex: never inspect live lists.
    class ResourceCompileSet final : public osgUtil::IncrementalCompileOperation::CompileSet
    {
    public:
        explicit ResourceCompileSet(osg::Node* node)
            : CompileSet(node) {}
        void captureResources();
        // Only before enqueue: live compile maps belong to the graphics thread.
        void splitCompileOperations();
        const std::unordered_set<const osg::Object*>& resources() const { return mResources; }

    private:
        std::unordered_set<const osg::Object*> mResources;
    };

    osg::ref_ptr<ResourceCompileSet> prepareCompileSet(osgUtil::IncrementalCompileOperation& operation,
        osg::Node* node, osgUtil::StateToCompile* state = nullptr);
    void enqueueCompile(osgUtil::IncrementalCompileOperation& operation, osg::Node* node);

    struct CompileReadiness
    {
        osg::ref_ptr<ResourceCompileSet> mSet;
        std::shared_ptr<std::atomic_bool> mReady;
        std::size_t mEstimatedBytes = 0;
    };
    // Unlike the shared ICO marker, this gate represents the completion callback
    // for every captured context. It is not a GPU fence or a shader-link check.
    CompileReadiness prepareCompileReadiness(
        osg::Node* node, osgUtil::IncrementalCompileOperation::ContextSet contexts);

    // Owner-thread receipts for immutable nodes whose compile callback completed.
    // These do not retain scene graphs or occupy the preparation queue's budget.
    // The owner must clear them when the resources or graphics contexts change.
    class CompileReadinessCache
    {
    public:
        explicit CompileReadinessCache(std::size_t maximumEntries = 512) : mMaximumEntries(maximumEntries) {}
        bool contains(const osg::ref_ptr<osg::Node>& node);
        void remember(const osg::ref_ptr<osg::Node>& node);
        void clear() { mEntries.clear(); }
        std::size_t size() const { return mEntries.size(); }

    private:
        struct Entry
        {
            osg::observer_ptr<osg::Node> mNode;
            std::uint64_t mLastUse;
        };
        const std::size_t mMaximumEntries;
        std::uint64_t mUse = 0;
        std::unordered_map<const osg::Node*, Entry> mEntries;
    };

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
