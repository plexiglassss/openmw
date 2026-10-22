#include <chrono>
#include <cmath>
#include <components/terrain/buffercache.hpp>
#include <gtest/gtest.h>
#include <iostream>
#include <osg/Geometry>
#include <osg/KdTree>
#include <osg/MatrixTransform>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>

TEST(TerrainRayAcceleration, PreservesStitchedTerrainHits)
{
    Terrain::BufferCache buffers;
    double linearMs = 0, indexedMs = 0;
    unsigned compared = 0;
    for (unsigned flags : { 0u, 1u, 0x1111u, 1u << 16 })
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        for (int y = 0; y < 65; ++y)
            for (int x = 0; x < 65; ++x)
                vertices->push_back(osg::Vec3(x, y, std::sin(x * 0.2) * std::cos(y * 0.2)));
        geometry->setVertexArray(vertices);
        geometry->addPrimitiveSet(buffers.getIndexBuffer(65, flags));
        osg::ref_ptr<osg::KdTree> tree = new osg::KdTree;
        osg::KdTree::BuildOptions options;
        ASSERT_TRUE(tree->build(options, geometry));
        geometry->setShape(tree);
        osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform;
        transform->setMatrix(osg::Matrix::scale(2, 3, 1.5) * osg::Matrix::translate(-100, 200, 30));
        transform->addChild(geometry);
        for (unsigned n = 0; n < 300; ++n)
        {
            // Include misses, oblique rays, boundaries and both travel directions.
            const double x = (n % 70) - 3.0, y = ((n * 17) % 70) - 3.0;
            osg::Vec3d start(x, y, n % 2 ? -20 : 20), end(x + 0.17, y + 0.23, n % 2 ? 20 : -20);
            start = start * transform->getMatrix();
            end = end * transform->getMatrix();
            auto cast = [&](bool indexed) {
                osg::ref_ptr<osgUtil::LineSegmentIntersector> ray = new osgUtil::LineSegmentIntersector(start, end);
                ray->setIntersectionLimit(osgUtil::Intersector::LIMIT_NEAREST);
                osgUtil::IntersectionVisitor visitor(ray);
                visitor.setUseKdTreeWhenAvailable(indexed);
                const auto begin = std::chrono::steady_clock::now();
                transform->accept(visitor);
                const double ms
                    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
                (indexed ? indexedMs : linearMs) += ms;
                return ray;
            };
            const auto a = cast(false), b = cast(true);
            ASSERT_TRUE(a->containsIntersections() == b->containsIntersections());
            if (a->containsIntersections())
            {
                const auto& ah = a->getFirstIntersection();
                const auto& bh = b->getFirstIntersection();
                ASSERT_TRUE(std::abs(ah.ratio - bh.ratio) < 1e-6);
                ASSERT_TRUE((ah.getWorldIntersectPoint() - bh.getWorldIntersectPoint()).length() < 1e-5);
                ASSERT_TRUE((ah.getWorldIntersectNormal() - bh.getWorldIntersectNormal()).length() < 1e-5);
                ASSERT_TRUE(ah.indexList == bh.indexList);
            }
            ++compared;
        }
    }
    std::cout << compared << " terrain rays match; linear " << linearMs << " ms, indexed " << indexedMs << " ms\n";
}
