#include <components/terrain/quadtreeworld.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/terrain/storage.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/compilepriority.hpp>
#include <components/settings/values.hpp>
#include <components/misc/combinedperformance.hpp>
#include <components/loadinglistener/reporter.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>
#include <osg/Geometry>
#include <osg/GraphicsContext>
#include <osg/State>
#include <osgUtil/SceneView>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>
#include <gtest/gtest.h>
#include <future>
#include <thread>

namespace
{
    class ArrivalContext : public osg::GraphicsContext
    {
    public:
        ArrivalContext() { setState(new osg::State); }
        ~ArrivalContext() override { setState(nullptr); }
        bool valid() const override { return true; }
        bool realizeImplementation() override { return false; }
        bool isRealizedImplementation() const override { return false; }
        void closeImplementation() override {}
        bool makeCurrentImplementation() override { return false; }
        bool makeContextCurrentImplementation(osg::GraphicsContext*) override { return false; }
        bool releaseContextImplementation() override { return false; }
        void bindPBufferToTextureImplementation(GLenum) override {}
        void swapBuffersImplementation() override {}
    };
    struct FlatStorage : Terrain::Storage
    {
        void getBounds(float& a, float& b, float& c, float& d, ESM::RefId) override { a=c=-4; b=d=4; }
        bool getMinMaxHeights(float, const osg::Vec2f&, ESM::RefId, float& a, float& b) override
        { a=b=0; return true; }
        float getHeightAt(const osg::Vec3f&, ESM::RefId) override { return 0; }
        float getCellWorldSize(ESM::RefId) override { return 8192; }
        int getCellVertices(ESM::RefId) override { return 65; }
        int getTextureTileCount(float, ESM::RefId) override { return 1; }
        void getBlendmaps(float, const osg::Vec2f&, ImageVector&, std::vector<Terrain::LayerInfo>&, ESM::RefId) override {}
        void fillVertexBuffers(int lod, float size, const osg::Vec2f&, ESM::RefId,
            osg::Vec3Array& p, osg::Vec3Array& n, osg::Vec4ubArray& c) override
        {
            const auto count = static_cast<unsigned>(64 * size / (1 << lod) + 1);
            for (unsigned y=0; y<count; ++y)
                for (unsigned x=0; x<count; ++x)
                {
                    p.push_back(osg::Vec3f((float(x)/(count-1)-.5f)*size*8192,
                        (float(y)/(count-1)-.5f)*size*8192, 0));
                    n.push_back(osg::Vec3f(0,0,1));
                    c.push_back(osg::Vec4ub(255,255,255,255));
                }
        }
    };
    struct RoofChunks : Terrain::QuadTreeWorld::ChunkManager
    {
        const std::thread::id mOwner = std::this_thread::get_id();
        std::atomic_uint mOwnerCalls{ 0 }, mWorkerFarCalls{ 0 };
        std::promise<void> mFarStarted, mReleaseFar;
        std::shared_future<void> mRelease = mReleaseFar.get_future().share();
        osg::ref_ptr<osg::Node> getChunk(float size, const osg::Vec2f&, unsigned char, unsigned,
            bool active, const osg::Vec3f&, bool, const std::atomic_bool*) override
        {
            if (std::this_thread::get_id() == mOwner)
                ++mOwnerCalls;
            else if (!active && ++mWorkerFarCalls == 1)
            {
                mFarStarted.set_value();
                mRelease.wait();
            }
            const float r = size * 8192 * .5f;
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
            vertices->push_back({-r,-r,10}); vertices->push_back({r,-r,10});
            vertices->push_back({r,r,10}); vertices->push_back({-r,r,10});
            geometry->setVertexArray(vertices);
            geometry->addPrimitiveSet(new osg::DrawArrays(GL_QUADS,0,4));
            geometry->setNodeMask(2);
            return geometry;
        }
        unsigned getNodeMask() override { return 2; }
        bool mayIntersectOutsideTile(const Terrain::QuadTreeWorld::ChunkRequest&,
            const osg::Vec3f&, const Terrain::RayFootprint&) override { return false; }
    };

    TEST(StagedTerrain, FirstCullAndNearRayReusePublishedCoverageWhileFarWorkerIsBlocked)
    {
        RecordProperty("staged_arrival_enabled", Misc::useStagedArrival() ? "true" : "false");
        VFS::Manager vfs;
        ToUTF8::StatelessUtf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resources(&vfs, 1.0, &encoder);
        osg::ref_ptr<ArrivalContext> context = new ArrivalContext;
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> compile = new osgUtil::IncrementalCompileOperation;
        compile->addGraphicsContext(context);
        resources.getSceneManager()->setIncrementalCompileOperation(compile);
        osg::ref_ptr<osg::Group> root = new osg::Group, compileRoot = new osg::Group;
        FlatStorage storage;
        Terrain::QuadTreeWorld world(root, compileRoot, &resources, &storage, 1, 1, 0,
            128, 128, 1, 0, 128, false, ESM::Cell::sDefaultWorldspaceId, 1.0);
        RoofChunks roofs;
        world.addChunkManager(&roofs);
        world.setViewDistance(32768);
        world.setActiveGrid({-1,-1,2,2});
        world.enable(true);
        auto arrival = world.createArrival({ 100, 100, 100 }, {-1,-1,2,2});
        auto view = world.createView();
        std::atomic_bool abort{ false };
        Loading::Reporter progress;
        std::promise<void> nearReady;
        auto worker = std::async(std::launch::async, [&] {
            world.preloadArrival(view.get(), arrival, abort, progress,
                [&](std::size_t, std::size_t) { nearReady.set_value(); });
        });
        struct Unblock
        {
            std::atomic_bool& abort;
            std::promise<void>& release;
            bool done = false;
            void stop() { if (!done) { abort = true; release.set_value(); done = true; } }
            ~Unblock() { stop(); }
        } unblock{ abort, roofs.mReleaseFar };
        const auto nearStatus = nearReady.get_future().wait_for(std::chrono::seconds(10));
        if (nearStatus != std::future_status::ready)
        {
            unblock.stop(); worker.get();
            FAIL() << "Required milestone did not arrive";
        }
        const auto farStatus = roofs.mFarStarted.get_future().wait_for(std::chrono::seconds(10));
        if (farStatus != std::future_status::ready)
        {
            unblock.stop(); worker.get();
            FAIL() << "Distant producer did not reach its blocking barrier";
        }
        EXPECT_EQ(arrival->activeReady(), Misc::useStagedArrival());
        EXPECT_FALSE(arrival->complete());
        world.showArrival(arrival);
        osg::ref_ptr<osg::FrameStamp> stamp = new osg::FrameStamp;
        stamp->setFrameNumber(1); stamp->setReferenceTime(1);
        const auto ray = [&](unsigned mask, float x, float y) {
            osg::ref_ptr<osgUtil::LineSegmentIntersector> segment =
                new osgUtil::LineSegmentIntersector(osg::Vec3d(x,y,100), osg::Vec3d(x,y,-100));
            osgUtil::IntersectionVisitor visitor(segment);
            visitor.setFrameStamp(stamp);
            visitor.setTraversalMask(mask);
            visitor.setReferenceEyePoint({100,100,100});
            visitor.setReferenceEyePointCoordinateFrame(osgUtil::Intersector::MODEL);
            world.accept(visitor);
            return segment;
        };
        // The query intentionally precedes the first camera cull.
        auto hit = ray(~0u, 120,120);
        EXPECT_TRUE(hit->containsIntersections());
        if (hit->containsIntersections())
        {
            EXPECT_NEAR(hit->getFirstIntersection().getWorldIntersectPoint().z(),10,1e-4);
        }
        hit = ray(1,120,120);
        EXPECT_TRUE(hit->containsIntersections());
        if (hit->containsIntersections())
        {
            EXPECT_NEAR(hit->getFirstIntersection().getWorldIntersectPoint().z(),0,1e-4);
        }
        EXPECT_EQ(roofs.mOwnerCalls,0u);
        osg::ref_ptr<osgUtil::SceneView> camera = new osgUtil::SceneView;
        camera->setDefaults();
        camera->setSceneData(root);
        camera->setFrameStamp(stamp);
        camera->setViewport(0,0,32,32);
        camera->setViewMatrixAsLookAt({100,100,100}, {100,200,0}, {0,0,1});
        camera->setProjectionMatrixAsPerspective(70,1,1,32768);
        camera->cull();
        if (Misc::useStagedArrival())
        {
            EXPECT_EQ(roofs.mOwnerCalls,0u);
        }
        else
        {
            EXPECT_GT(roofs.mOwnerCalls,0u); // Legacy camera synchronously completes far coverage.
        }
        // A far exact query must build a missing roof rather than accepting ground-only coverage.
        hit = ray(~0u, 22000,22000);
        EXPECT_TRUE(hit->containsIntersections());
        if (hit->containsIntersections())
        {
            EXPECT_NEAR(hit->getFirstIntersection().getWorldIntersectPoint().z(),10,1e-4);
        }
        EXPECT_GT(roofs.mOwnerCalls,0u);
        if (Misc::useStagedArrival())
        {
            context->getState()->setContextID(context->getState()->getContextID() + 1);
            camera->cull(); // same object, different GL generation
            EXPECT_TRUE(arrival->cancelled());
        }
        unblock.stop(); worker.get();
        if (Misc::useStagedArrival())
        {
            // Populate a complete camera view via the ordinary path, then start
            // another arrival for exactly that coverage. It must not park forever
            // waiting to admit nodes which the camera already owns.
            world.showArrival({});
            camera->cull();
            const auto ownerCalls = roofs.mOwnerCalls.load();
            auto warm = world.createArrival({100,100,100}, {-1,-1,2,2});
            auto warmView = world.createView();
            Loading::Reporter warmProgress;
            std::atomic_bool warmAbort{ false };
            std::promise<void> warmNear;
            auto warmWorker = std::async(std::launch::async, [&] {
                world.preloadArrival(warmView.get(), warm, warmAbort, warmProgress,
                    [&](std::size_t, std::size_t) { warmNear.set_value(); });
            });
            struct CancelWork { std::atomic_bool& abort; ~CancelWork() { abort = true; } } cancel{warmAbort};
            const auto warmNearStatus = warmNear.get_future().wait_for(std::chrono::seconds(10));
            if (warmNearStatus == std::future_status::ready)
            {
                world.showArrival(warm);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (warmWorker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready
                    && std::chrono::steady_clock::now() < deadline)
                {
                    stamp->setFrameNumber(stamp->getFrameNumber()+1);
                    camera->cull();
                    // This fixture tests ownership/scheduling, not actual GL. Simulate
                    // completion callbacks after a frame, as a real graphics context does.
                    osgUtil::IncrementalCompileOperation::CompileSets pending;
                    {
                        const std::lock_guard lock(*compile->getToCompiledMutex());
                        pending = compile->getToCompile();
                    }
                    std::size_t callbacks = 0;
                    for (const auto& set : pending)
                        if (set->_compileCompletedCallback)
                        {
                            ++callbacks;
                            set->_compileCompletedCallback->compileCompleted(set);
                            compile->remove(set);
                        }
                    EXPECT_LE(callbacks, 8u);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            const bool completed = warmWorker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
            warmAbort = true;
            warmWorker.get();
            EXPECT_EQ(warmNearStatus, std::future_status::ready);
            EXPECT_TRUE(completed);
            EXPECT_EQ(roofs.mOwnerCalls, ownerCalls);
        }
        resources.getSceneManager()->releaseGLObjects(nullptr);
        EXPECT_EQ(world.validArrival(*arrival), !Misc::useStagedArrival());
    }
}
