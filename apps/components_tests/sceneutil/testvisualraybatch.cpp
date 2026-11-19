#include <components/sceneutil/visualraybatch.hpp>

#include <gtest/gtest.h>

#include <osg/Billboard>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/LOD>
#include <osg/MatrixTransform>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
    using Ray = osgUtil::LineSegmentIntersector;
    using Intersector = osgUtil::Intersector;
    using Endpoints = std::vector<osg::Vec3f>;

    struct Options
    {
        std::string ignore;
        bool paged = false;
        bool eyeLod = false;
        unsigned int mask = ~0u;
    };

    class IgnoreVisitor : public osgUtil::IntersectionVisitor
    {
    public:
        IgnoreVisitor(Intersector* intersector, const Options& options)
            : osgUtil::IntersectionVisitor(intersector)
            , mOptions(options)
        {
            setUseKdTreeWhenAvailable(true);
            setTraversalMask(options.mask);
            if (options.eyeLod)
                setLODSelectionMode(USE_EYE_POINT_FOR_LOD_LEVEL_SELECTION);
        }

        void apply(osg::Transform& transform) override
        {
            if (!mOptions.paged && !mOptions.ignore.empty() && transform.getName() == mOptions.ignore)
                return;
            osgUtil::IntersectionVisitor::apply(transform);
        }

    private:
        Options mOptions;
    };

    class CountingRoot : public osg::Group
    {
    public:
        unsigned int visits = 0;

        void traverse(osg::NodeVisitor& visitor) override
        {
            ++visits;
            osg::Group::traverse(visitor);
        }
    };

    // QuadTreeWorld also uses the visitor's MODEL eye when selecting terrain.
    class EyeSelectedTiles : public osg::Group
    {
    public:
        void traverse(osg::NodeVisitor& visitor) override
        {
            getChild(visitor.getEyePoint().z() > 20.f ? 1 : 0)->accept(visitor);
        }
    };

    osg::ref_ptr<osg::Geometry> square(const std::string& name, float half)
    {
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        vertices->push_back({ -half, -half, 0 });
        vertices->push_back({ half, -half, 0 });
        vertices->push_back({ half, half, 0 });
        vertices->push_back({ -half, half, 0 });

        osg::ref_ptr<osg::DrawElementsUInt> elements = new osg::DrawElementsUInt(GL_TRIANGLES);
        for (unsigned int index : { 0u, 1u, 2u, 0u, 2u, 3u })
            elements->push_back(index);
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        geometry->setName(name);
        geometry->setVertexArray(vertices);
        geometry->addPrimitiveSet(elements);
        return geometry;
    }

    osg::ref_ptr<osg::MatrixTransform> plane(
        const std::string& name, const osg::Vec3f& center, float extent = 2.f, unsigned int mask = ~0u)
    {
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->setName(name + " geode");
        geode->addDrawable(square(name, extent));
        osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform;
        transform->setName(name);
        transform->setMatrix(osg::Matrix::translate(center));
        transform->setNodeMask(mask);
        transform->addChild(geode);
        return transform;
    }

    struct Hit
    {
        bool found = false;
        double ratio = 0.;
        osg::Vec3d point;
        osg::Vec3d normal;
        const osg::Drawable* drawable = nullptr;
        osg::NodePath path;
        unsigned int primitive = 0;
        std::vector<unsigned int> indices;
    };

    Hit firstEligible(Ray& ray, const Options& options)
    {
        for (const auto& hit : ray.getIntersections())
        {
            if (options.paged && !options.ignore.empty()
                && std::any_of(hit.nodePath.begin(), hit.nodePath.end(),
                    [&](const osg::Node* node) { return node->getName() == options.ignore; }))
                continue;

            return { true, hit.ratio, hit.getWorldIntersectPoint(), hit.getWorldIntersectNormal(),
                hit.drawable.get(), hit.nodePath, hit.primitiveIndex, hit.indexList };
        }
        return {};
    }

    Hit scalar(osg::Node& root, const osg::Vec3f& origin, const osg::Vec3f& end, const Options& options)
    {
        osg::ref_ptr<Ray> ray = new Ray(Intersector::MODEL, origin, end);
        ray->setIntersectionLimit(options.paged ? Intersector::NO_LIMIT : Intersector::LIMIT_NEAREST);
        IgnoreVisitor visitor(ray, options);
        visitor.setReferenceEyePoint(origin);
        visitor.setReferenceEyePointCoordinateFrame(Intersector::MODEL);
        root.accept(visitor);
        return firstEligible(*ray, options);
    }

    void expectSame(const Hit& expected, const Hit& actual)
    {
        ASSERT_EQ(actual.found, expected.found);
        if (!expected.found)
            return;
        EXPECT_NEAR(actual.ratio, expected.ratio, 1e-12);
        EXPECT_LT((actual.point - expected.point).length2(), 1e-16);
        EXPECT_LT((actual.normal - expected.normal).length2(), 1e-12);
        EXPECT_EQ(actual.drawable, expected.drawable);
        EXPECT_EQ(actual.path, expected.path);
        EXPECT_EQ(actual.primitive, expected.primitive);
        EXPECT_EQ(actual.indices, expected.indices);
    }

    SceneUtil::VisualRayBatch batch(
        osg::Node& root, const osg::Vec3f& origin, const Endpoints& ends, const Options& options = {})
    {
        osg::ref_ptr<osgUtil::IntersectorGroup> group = new osgUtil::IntersectorGroup;
        group->setIntersectionLimit(options.paged ? Intersector::NO_LIMIT : Intersector::LIMIT_NEAREST);
        IgnoreVisitor visitor(group, options);
        return SceneUtil::intersectVisualRays(root, visitor, *group, origin, ends);
    }

    void compareToScalar(osg::Node& root, const osg::Vec3f& origin, const Endpoints& ends,
        const SceneUtil::VisualRayBatch& result, const Options& options = {})
    {
        ASSERT_EQ(result.order.size(), ends.size());
        for (std::size_t i = 0; i < ends.size(); ++i)
        {
            SCOPED_TRACE(i);
            ASSERT_LT(result.order[i], result.segments.size());
            Ray& ray = *result.segments[result.order[i]];
            EXPECT_EQ(ray.getIntersectionLimit(), options.paged ? Intersector::NO_LIMIT : Intersector::LIMIT_NEAREST);
            expectSame(scalar(root, origin, ends[i], options), firstEligible(ray, options));
        }
    }

    const osg::Vec3f nearEye{ 0, 0, 12 };
    const Endpoints pick{ { .15f, .11f, -7 }, { -.6f, .31f, -7 } };

    TEST(VisualRayBatch, TraversesOnceAndRestoresOrderedHitsMissesAndDuplicates)
    {
        osg::ref_ptr<CountingRoot> root = new CountingRoot;
        root->setName("world");
        root->addChild(plane("front", { 0, 0, 6 }, 1.2f, 1));
        root->addChild(plane("ground", { 0, 0, 0 }, 8.f, 2));
        const Endpoints ends{ { .11f, .05f, -6 }, { 4.2f, 1.9f, -6 }, { -6, .46f, -5 },
            { 40, 20, -5 }, { .11f, .05f, -6 }, { 4.2f, 1.9f, -6 } };
        const auto result = batch(*root, nearEye, ends);
        EXPECT_EQ(root->visits, 1u);
        ASSERT_EQ(result.segments.size(), 4u);
        EXPECT_EQ(result.order, (std::vector<std::size_t>{ 0, 1, 2, 3, 0, 1 }));
        compareToScalar(*root, nearEye, ends, result);
        EXPECT_EQ(root->visits, ends.size() + 1);
        ASSERT_TRUE(result.segments[0]->containsIntersections());
        ASSERT_TRUE(result.segments[2]->containsIntersections());
        EXPECT_EQ(result.segments[0]->getFirstIntersection().drawable->getName(), "front");
        EXPECT_EQ(result.segments[2]->getFirstIntersection().drawable->getName(), "ground");
        EXPECT_FALSE(result.segments[3]->containsIntersections());

        for (const Options& options : { Options{ "front" }, Options{ "", false, false, 2 } })
        {
            const auto masked = batch(*root, nearEye, ends, options);
            compareToScalar(*root, nearEye, ends, masked, options);
            ASSERT_FALSE(masked.segments.empty());
            ASSERT_TRUE(masked.segments.front()->containsIntersections());
            EXPECT_EQ(masked.segments.front()->getFirstIntersection().drawable->getName(), "ground");
        }
    }

    TEST(VisualRayBatch, PagedIgnoreRetainsEligibleFartherHitOnEveryChild)
    {
        osg::ref_ptr<osg::Group> root = new osg::Group;
        root->addChild(plane("ignored", { 0, 0, 6 }));
        root->addChild(plane("eligible", { 0, 0, 1 }, .75f));
        const Options options{ "ignored", true };
        const auto result = batch(*root, nearEye, pick, options);
        compareToScalar(*root, nearEye, pick, result, options);
        ASSERT_EQ(result.segments.size(), pick.size());
        for (const auto& segment : result.segments)
        {
            const auto hit = firstEligible(*segment, options);
            ASSERT_NE(hit.drawable, nullptr);
            EXPECT_EQ(hit.drawable->getName(), "eligible");
        }
    }

    TEST(VisualRayBatch, PreservesNestedWorldNormalsAndEqualDistanceTraversalOrder)
    {
        osg::ref_ptr<osg::MatrixTransform> outer = new osg::MatrixTransform;
        outer->setMatrix(osg::Matrix::scale(2, 1, .9) * osg::Matrix::rotate(.21, osg::Vec3d(1, 0, 0))
            * osg::Matrix::translate(2, 1, 3));
        auto nested = plane("nested", { .8f, .45f, 1 }, .8f);
        outer->addChild(nested);
        const osg::Vec3f target(osg::Vec3d(.09, .12, 0) * nested->getMatrix() * outer->getMatrix());
        const osg::Vec3f direction = target - nearEye;
        const Endpoints nestedEnds{ nearEye + direction * 1.6f, nearEye + direction * 1.7f };
        const auto transformed = batch(*outer, nearEye, nestedEnds);
        compareToScalar(*outer, nearEye, nestedEnds, transformed);
        ASSERT_FALSE(transformed.segments.empty());
        const auto transformedHit = firstEligible(*transformed.segments.front(), {});
        ASSERT_NE(transformedHit.drawable, nullptr);
        EXPECT_EQ(transformedHit.drawable->getName(), "nested");
        EXPECT_GT(std::abs(transformedHit.normal.y()), .05);

        osg::ref_ptr<osg::Group> ties = new osg::Group;
        ties->addChild(plane("equal A", { 0, 0, 3 }, 4));
        ties->addChild(plane("equal B", { 0, 0, 3 }, 4));
        const auto result = batch(*ties, nearEye, pick);
        compareToScalar(*ties, nearEye, pick, result);
        ASSERT_FALSE(result.segments.empty());
        ASSERT_TRUE(result.segments.front()->containsIntersections());
        EXPECT_EQ(result.segments.front()->getFirstIntersection().drawable->getName(), "equal A");
    }

    TEST(VisualRayBatch, UsesModelEyeForTerrainLodAndBillboards)
    {
        osg::ref_ptr<EyeSelectedTiles> tiles = new EyeSelectedTiles;
        tiles->setCullingActive(false);
        tiles->addChild(plane("near tile", { 0, 0, 5 }, 4));
        tiles->addChild(plane("far tile", { 0, 0, 1 }, 4));
        osg::ref_ptr<osg::LOD> lod = new osg::LOD;
        lod->setCenter({ 0, 0, 0 });
        lod->addChild(plane("hi", { 0, 0, 5 }, 4), 0, 20);
        lod->addChild(plane("lo", { 0, 0, 1 }, 4), 20, 1000);
        osg::ref_ptr<osg::Billboard> billboard = new osg::Billboard;
        billboard->setMode(osg::Billboard::POINT_ROT_EYE);
        billboard->setNormal({ 0, 0, 1 });
        billboard->addDrawable(square("billboard", 3), { 0, 0, 2 });

        for (const osg::Vec3f& eye : { nearEye, osg::Vec3f{ 0, 0, 40 } })
        {
            const auto terrainResult = batch(*tiles, eye, pick);
            compareToScalar(*tiles, eye, pick, terrainResult);
            ASSERT_FALSE(terrainResult.segments.empty());
            ASSERT_TRUE(terrainResult.segments.front()->containsIntersections());
            EXPECT_EQ(terrainResult.segments.front()->getFirstIntersection().drawable->getName(),
                eye == nearEye ? "near tile" : "far tile");
            const Options options{ "", false, true };
            compareToScalar(*lod, eye, pick, batch(*lod, eye, pick, options), options);
            const auto billboardResult = batch(*billboard, eye, pick);
            compareToScalar(*billboard, eye, pick, billboardResult);
            ASSERT_FALSE(billboardResult.segments.empty());
            EXPECT_TRUE(billboardResult.segments.front()->containsIntersections());
        }
    }
}
