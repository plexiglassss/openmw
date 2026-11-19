#include "visualraybatch.hpp"

#include <algorithm>

namespace SceneUtil
{
    VisualRayBatch intersectVisualRays(osg::Node& root, osgUtil::IntersectionVisitor& visitor,
        osgUtil::IntersectorGroup& group, const osg::Vec3f& origin, std::span<const osg::Vec3f> ends)
    {
        using Segment = osgUtil::LineSegmentIntersector;
        visitor.setReferenceEyePoint(origin);
        visitor.setReferenceEyePointCoordinateFrame(osgUtil::Intersector::MODEL);
        VisualRayBatch result;
        auto& segments = result.segments;
        segments.reserve(ends.size());
        auto& index = result.order;
        index.reserve(ends.size());
        for (std::size_t i = 0; i < ends.size(); ++i)
        {
            const auto previous = std::find(ends.begin(), ends.begin() + i, ends[i]);
            if (previous != ends.begin() + i)
            {
                index.push_back(index[previous - ends.begin()]);
                continue;
            }
            osg::ref_ptr<Segment> ray = new Segment(osgUtil::Intersector::MODEL, origin, ends[i]);
            // Paged ignores require all hits on each independently cloned child.
            ray->setIntersectionLimit(group.getIntersectionLimit());
            group.addIntersector(ray);
            index.push_back(segments.size());
            segments.push_back(std::move(ray));
        }
        root.accept(visitor);
        return result;
    }
}
