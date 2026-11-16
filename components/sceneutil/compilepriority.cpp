#include "compilepriority.hpp"

#include <osg/Drawable>
#include <osg/Program>
#include <osg/StateSet>
#include <osg/Texture>
#include <osg/Geometry>
#include <osg/GraphicsContext>
#include <osg/State>

#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace SceneUtil
{
    void ResourceCompileSet::captureResources()
    {
        using Operation = osgUtil::IncrementalCompileOperation;
        for (const auto& [context, list] : _compileMap)
            for (const auto& op : list._compileOps)
            {
                if (const auto* drawable = dynamic_cast<const Operation::CompileDrawableOp*>(op.get()))
                    mResources.insert(drawable->_drawable.get());
                else if (const auto* texture = dynamic_cast<const Operation::CompileTextureOp*>(op.get()))
                    mResources.insert(texture->_texture.get());
                else if (const auto* program = dynamic_cast<const Operation::CompileProgramOp*>(op.get()))
                    mResources.insert(program->_program.get());
            }
    }

    osg::ref_ptr<ResourceCompileSet> prepareCompileSet(osgUtil::IncrementalCompileOperation& operation,
        osg::Node* node, osgUtil::StateToCompile* state)
    {
        osg::ref_ptr<ResourceCompileSet> result = new ResourceCompileSet(node);
        result->_markerObject = operation.getMarkerObject();
        node->getBound();
        if (state)
            result->buildCompileMap(operation.getContextSet(), *state);
        else
            result->buildCompileMap(operation.getContextSet());
        result->captureResources();
        return result;
    }

    void enqueueCompile(osgUtil::IncrementalCompileOperation& operation, osg::Node* node)
    {
        operation.add(prepareCompileSet(operation, node), false);
    }

    CompileReadiness prepareCompileReadiness(
        osg::Node* node, osgUtil::IncrementalCompileOperation::ContextSet contexts)
    {
        if (!node || contexts.empty())
            throw std::invalid_argument("Compile readiness requires a node and graphics contexts");
        struct Complete final : osgUtil::IncrementalCompileOperation::CompileCompletedCallback
        {
            std::shared_ptr<std::atomic_bool> mReady;
            std::vector<osg::ref_ptr<osg::GraphicsContext>> mContexts;
            bool compileCompleted(osgUtil::IncrementalCompileOperation::CompileSet*) override
            {
                mReady->store(true, std::memory_order_release);
                return true;
            }
        };
        osg::ref_ptr<Complete> complete = new Complete;
        for (auto* context : contexts)
        {
            if (!context || !context->getState())
                throw std::invalid_argument("Compile readiness requires initialized graphics contexts");
            complete->mContexts.emplace_back(context);
        }
        CompileReadiness result;
        result.mReady = std::make_shared<std::atomic_bool>(false);
        complete->mReady = result.mReady;
        // A shared marker means queued, not completed. Enumerate all dependencies
        // without changing markers that another compile operation may be using.
        osgUtil::StateToCompile state(osgUtil::GLObjectsVisitor::COMPILE_DISPLAY_LISTS
            | osgUtil::GLObjectsVisitor::COMPILE_STATE_ATTRIBUTES, nullptr);
        state.setNodeMaskOverride(~0u);
        state._assignPBOToImages = false;
        node->accept(state);
        result.mSet = new ResourceCompileSet(node);
        result.mSet->buildCompileMap(contexts, state);
        result.mSet->captureResources();
        result.mSet->_compileCompletedCallback = complete;
        result.mEstimatedBytes = state._drawablesHandled.size() * 512 + state._statesetsHandled.size() * 512;
        std::unordered_set<const osg::BufferData*> counted;
        for (const auto* drawable : state._drawables)
            if (const auto* geometry = drawable->asGeometry())
            {
                osg::Geometry::ArrayList arrays;
                geometry->getArrayList(arrays);
                for (const auto& array : arrays)
                    if (array && counted.insert(array.get()).second)
                        result.mEstimatedBytes += array->getTotalDataSize();
            }
        // Shared textures may already be applying on the graphics thread and
        // unref their images. Never inspect their mutable image slots here.
        // Use an admission estimate, not a claim of measured RAM/VRAM usage.
        result.mEstimatedBytes += state._textures.size() * 4ull * 1024 * 1024;
        return result;
    }

    bool CompileReadinessCache::contains(const osg::ref_ptr<osg::Node>& node)
    {
        const auto found = mEntries.find(node.get());
        if (found == mEntries.end())
            return false;
        osg::ref_ptr<osg::Node> observed;
        if (!found->second.mNode.lock(observed) || observed != node)
        {
            // A recycled address must not inherit the previous node's receipt.
            mEntries.erase(found);
            return false;
        }
        found->second.mLastUse = ++mUse;
        return true;
    }

    void CompileReadinessCache::remember(const osg::ref_ptr<osg::Node>& node)
    {
        if (!node || !mMaximumEntries)
            return;
        if (!mEntries.contains(node.get()) && mEntries.size() >= mMaximumEntries)
        {
            const auto oldest = std::min_element(mEntries.begin(), mEntries.end(),
                [](const auto& a, const auto& b) { return a.second.mLastUse < b.second.mLastUse; });
            // Evict only a weak receipt. Locking arbitrary old nodes here could
            // make their final heavy graph destruction run on the owner thread.
            mEntries.erase(oldest);
        }
        mEntries.insert_or_assign(node.get(), Entry{ osg::observer_ptr<osg::Node>(node), ++mUse });
    }

    CompilePriorityVisitor::CompilePriorityVisitor()
        : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
    {
        setNodeMaskOverride(~0u);
    }

    void CompilePriorityVisitor::collect(const osg::StateSet* state)
    {
        if (!state || !mResources.insert(state).second)
            return;
        for (const auto& [key, value] : state->getAttributeList())
            mResources.insert(value.first.get());
        for (const auto& unit : state->getTextureAttributeList())
            for (const auto& [key, value] : unit)
                mResources.insert(value.first.get());
    }

    void CompilePriorityVisitor::apply(osg::Node& node)
    {
        if (!mResources.insert(&node).second)
            return;
        collect(node.getStateSet());
        traverse(node);
    }

    void CompilePriorityVisitor::apply(osg::Drawable& drawable)
    {
        apply(static_cast<osg::Node&>(drawable));
    }

    std::size_t CompilePriorityVisitor::promote(osgUtil::IncrementalCompileOperation& operation) const
    {
        osgUtil::IncrementalCompileOperation::CompileSets priority;
        const std::lock_guard lock(*operation.getToCompiledMutex());
        auto& pending = operation.getToCompile();
        for (auto it = pending.begin(); it != pending.end();)
        {
            const auto current = it++;
            bool needed = mResources.contains((*current)->_subgraphToCompile.get());
            if (!needed)
                if (const auto* set = dynamic_cast<const ResourceCompileSet*>(current->get()))
                    for (const auto* resource : set->resources())
                        if (mResources.contains(resource))
                        {
                            needed = true;
                            break;
                        }
            if (needed)
                priority.splice(priority.end(), pending, current);
        }
        const auto count = priority.size();
        pending.splice(pending.begin(), priority);
        return count;
    }
}
