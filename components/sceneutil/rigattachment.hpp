#ifndef OPENMW_COMPONENTS_SCENEUTIL_RIGATTACHMENT_H
#define OPENMW_COMPONENTS_SCENEUTIL_RIGATTACHMENT_H

#include <osg/Drawable>
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/NodeVisitor>

#include <components/misc/strings/algorithm.hpp>

#include <set>
#include <string_view>
#include <vector>

namespace SceneUtil
{
    // A recipe contains source nodes only. Every use still clones them into a fresh
    // actor; no live skeleton, controller, animation time or equipment state is shared.
    struct RigAttachmentPlan
    {
        osg::ref_ptr<const osg::Node> mTemplate;
        std::vector<osg::ref_ptr<const osg::Node>> mNodes;
    };

    template <class IsRig>
    RigAttachmentPlan planRigAttachment(const osg::Node& source, std::string_view filter, IsRig isRig)
    {
        class Visitor : public osg::NodeVisitor
        {
            std::string_view mFilter;
            IsRig mIsRig;

            bool matches(std::string_view name) const
            {
                return Misc::StringUtils::ciStartsWith(name, mFilter)
                    || (Misc::StringUtils::ciStartsWith(name, "tri ")
                        && Misc::StringUtils::ciStartsWith(name.substr(4), mFilter));
            }

        public:
            std::set<osg::ref_ptr<const osg::Node>> mNodes;
            Visitor(std::string_view filter, IsRig predicate)
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN), mFilter(filter), mIsRig(predicate)
            {
            }
            void apply(osg::MatrixTransform& node) override { traverse(node); }
            void apply(osg::Node& node) override { traverse(node); }
            void apply(osg::Group& node) override { traverse(node); }
            void apply(osg::Drawable& drawable) override
            {
                if (!matches(drawable.getName()) || !mIsRig(drawable))
                    return;
                const osg::Node* node = &drawable;
                for (auto it = getNodePath().rbegin() + 1; it != getNodePath().rend(); ++it)
                {
                    if (!matches((*it)->getName()))
                        break;
                    node = *it;
                }
                mNodes.emplace(node);
            }
        };

        Visitor visitor(filter, isRig);
        const_cast<osg::Node&>(source).accept(visitor);
        return { &source, { visitor.mNodes.begin(), visitor.mNodes.end() } };
    }
}
#endif
