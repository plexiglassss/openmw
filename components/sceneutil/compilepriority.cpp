#include "compilepriority.hpp"

#include <osg/Drawable>
#include <osg/Program>
#include <osg/StateSet>
#include <osg/Texture>

#include <mutex>

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
