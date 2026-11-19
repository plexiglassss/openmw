#ifndef OPENMW_COMPONENTS_SCENEUTIL_VISUALRAYBATCH_H
#define OPENMW_COMPONENTS_SCENEUTIL_VISUALRAYBATCH_H

#include <osg/Node>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>

#include <span>
#include <vector>

namespace SceneUtil
{
    struct VisualRayBatch
    {
        std::vector<osg::ref_ptr<osgUtil::LineSegmentIntersector>> segments;
        std::vector<std::size_t> order;
    };

    // The owner has already installed the traversal mask, ignore visitor and paged-hit limit.
    VisualRayBatch intersectVisualRays(osg::Node& root, osgUtil::IntersectionVisitor& visitor,
        osgUtil::IntersectorGroup& group, const osg::Vec3f& origin, std::span<const osg::Vec3f> ends);
}
#endif
