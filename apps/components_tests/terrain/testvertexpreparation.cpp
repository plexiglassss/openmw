#include <components/terrain/vertexbuffercache.hpp>
#include <components/terrain/buffercache.hpp>
#include <components/terrain/storage.hpp>
#include <components/misc/nativejobpool.hpp>

#include <gtest/gtest.h>
#include <osg/KdTree>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace std::chrono_literals;
    const Terrain::VertexBufferKey key{ { 0.5f, 0.5f }, 1.f, 0 };

    struct Storage : Terrain::Storage
    {
        std::atomic_uint calls{ 0 };
        std::atomic<float> height{ 1.f };
        std::function<void()> onFill;
        void getBounds(float& a, float& b, float& c, float& d, ESM::RefId) override { a=c=0; b=d=4; }
        bool getMinMaxHeights(float, const osg::Vec2f&, ESM::RefId, float& a, float& b) override { a=b=0; return true; }
        float getHeightAt(const osg::Vec3f&, ESM::RefId) override { return height; }
        float getCellWorldSize(ESM::RefId) override { return 8192; }
        int getCellVertices(ESM::RefId) override { return 5; }
        int getTextureTileCount(float, ESM::RefId) override { return 1; }
        void getBlendmaps(float, const osg::Vec2f&, ImageVector&, std::vector<Terrain::LayerInfo>&, ESM::RefId) override {}
        void fillVertexBuffers(int lod, float size, const osg::Vec2f& center, ESM::RefId,
            osg::Vec3Array& p, osg::Vec3Array& n, osg::Vec4ubArray& c) override
        {
            ++calls;
            const float z = height.load();
            if (onFill) onFill();
            for (unsigned y=0; y<5; ++y)
                for (unsigned x=0; x<5; ++x)
                {
                    p.push_back(osg::Vec3f(center.x()+x*size, center.y()+y*size, z+lod+(x*y)*.03f));
                    n.push_back(osg::Vec3f(0,0,1));
                    c.push_back(osg::Vec4ub(20,40,220,255));
                }
        }
    };

    struct Gate
    {
        std::mutex mutex;
        std::condition_variable changed;
        unsigned entered=0;
        bool open=false;
        void enter()
        {
            std::unique_lock lock(mutex);
            ++entered; changed.notify_all();
            if (!changed.wait_for(lock, 5s, [&]{return open;}))
                throw std::runtime_error("test gate timeout");
        }
        bool wait(unsigned n)
        {
            std::unique_lock lock(mutex);
            return changed.wait_for(lock, 5s, [&]{return entered>=n;});
        }
        void release() { std::lock_guard lock(mutex);open=true;changed.notify_all(); }
        ~Gate() { release(); }
    };

    TEST(TerrainVertexPreparation, ConcurrentIdenticalRequestsFillOnce)
    {
        Storage storage; Gate gate;
        storage.onFill=[&]{gate.enter();};
        Terrain::VertexBufferCache cache(storage, {});
        std::array<std::future<Terrain::VertexBufferCache::Value>, 8> results;
        for (auto& result:results)
            result=std::async(std::launch::async,[&]{return cache.get(key);});
        EXPECT_TRUE(gate.wait(1));
        const auto until=std::chrono::steady_clock::now()+5s;
        // Every requester has passed its cache lookup while the producer is blocked.
        while (cache.getStats().mGet<results.size()+1 && std::chrono::steady_clock::now()<until)
            std::this_thread::yield();
        EXPECT_GE(cache.getStats().mGet, results.size()+1);
        gate.release();
        const auto first=results.front().get();
        for (std::size_t i=1;i<results.size();++i) EXPECT_EQ(results[i].get(), first);
        EXPECT_EQ(storage.calls, 1u);
        EXPECT_EQ(first->mPositions->getBinding(), osg::Array::BIND_PER_VERTEX);
        EXPECT_TRUE(first->mColours->getNormalize());
        EXPECT_EQ(first->mNormals->getVertexBufferObject(), first->mPositions->getVertexBufferObject());
    }

    TEST(TerrainVertexPreparation, IndependentChunksOverlapAndJoin)
    {
        Storage storage; Gate gate;
        storage.onFill=[&]{gate.enter();};
        Terrain::VertexBufferCache cache(storage, {});
        Misc::NativeJobPool jobs(3,1);
        std::array<Terrain::VertexBufferKey,4> keys{key,key,key,key};
        for (unsigned i=0;i<keys.size();++i) keys[i].mCenter.x()+=i;
        std::atomic_bool abort=false;
        auto prepare=std::async(std::launch::async,[&]{cache.prepare(keys,jobs,abort);});
        EXPECT_TRUE(gate.wait(4));
        gate.release();prepare.get();
        EXPECT_EQ(storage.calls,4u);
        for (const auto& k:keys) EXPECT_EQ(cache.get(k)->mPositions->front().x(),k.mCenter.x());
        EXPECT_EQ(storage.calls,4u);
    }

    TEST(TerrainVertexPreparation, SerialAndNestedPoolFallbacksComplete)
    {
        Storage storage;Terrain::VertexBufferCache cache(storage,{});
        std::array<Terrain::VertexBufferKey,2> keys{key,key};keys[1].mSize=2;
        std::atomic_bool abort=false;
        Misc::NativeJobPool serial(0,1);cache.prepare(keys,serial,abort);
        EXPECT_EQ(storage.calls,2u);
        cache.clear();Misc::NativeJobPool parallel(1,1);
        std::promise<void> done;auto future=done.get_future();
        ASSERT_TRUE(parallel.trySubmit([&]{cache.prepare(keys,parallel,abort);done.set_value();}));
        EXPECT_EQ(future.wait_for(5s),std::future_status::ready);
        future.get();EXPECT_EQ(storage.calls,4u);
    }

    TEST(TerrainVertexPreparation, CancellationSkipsUnstartedWorkButJoinsLiveFills)
    {
        Storage storage;Gate gate;storage.onFill=[&]{gate.enter();};
        Terrain::VertexBufferCache cache(storage,{});Misc::NativeJobPool jobs(1,1);
        std::array<Terrain::VertexBufferKey,4> keys{key,key,key,key};
        for (unsigned i=0;i<keys.size();++i) keys[i].mCenter.x()+=i;
        std::atomic_bool abort=true;cache.prepare(keys,jobs,abort);EXPECT_EQ(storage.calls,0u);
        abort=false;auto prepare=std::async(std::launch::async,[&]{cache.prepare(keys,jobs,abort);});
        EXPECT_TRUE(gate.wait(2));abort=true;gate.release();prepare.get();
        EXPECT_EQ(storage.calls,2u);
        storage.onFill={};abort=false;cache.prepare(keys,jobs,abort);
        EXPECT_EQ(storage.calls,4u);
    }

    TEST(TerrainVertexPreparation, FailureDoesNotPublishPartialDataOrPoisonRetry)
    {
        Storage storage;storage.onFill=[] {throw std::runtime_error("injected fill failure");};
        Terrain::VertexBufferCache cache(storage,{});
        EXPECT_THROW(cache.get(key),std::runtime_error);EXPECT_EQ(cache.getStats().mSize,0u);
        Misc::NativeJobPool jobs(2,1);std::atomic_bool abort=false;
        cache.prepare(std::span(&key,1),jobs,abort);EXPECT_EQ(cache.getStats().mSize,0u);
        storage.onFill={};EXPECT_EQ(cache.get(key)->mPositions->size(),25u);
        EXPECT_EQ(storage.calls,3u);
    }

    TEST(TerrainVertexPreparation, ClearWaitsForProducerAndDiscardsOldData)
    {
        Storage storage;Gate gate;storage.onFill=[&]{gate.enter();};
        Terrain::VertexBufferCache cache(storage,{});
        auto producer=std::async(std::launch::async,[&]{return cache.get(key);});
        EXPECT_TRUE(gate.wait(1));storage.height=7;
        std::promise<void> clearing;auto started=clearing.get_future();
        auto clear=std::async(std::launch::async,[&]{clearing.set_value();cache.clear();});
        started.wait();
        EXPECT_EQ(clear.wait_for(50ms),std::future_status::timeout);
        gate.release();const auto old=producer.get();clear.get();storage.onFill={};
        EXPECT_EQ(old->mPositions->front().z(),1);
        EXPECT_EQ(cache.get(key)->mPositions->front().z(),7);
        EXPECT_EQ(storage.calls,2u);
    }

    TEST(TerrainVertexPreparation, IdentityIncludesSizeLodAndWorldOwner)
    {
        Storage storage;Terrain::VertexBufferCache cache(storage,{}),otherWorld(storage,{});
        const auto original=cache.get(key);
        auto changed=key;changed.mSize=2;
        EXPECT_NE(original,cache.get(changed));changed=key;changed.mLod=1;
        EXPECT_NE(original,cache.get(changed));EXPECT_NE(original,otherWorld.get(key));
        changed=key;changed.mCenter.x()=std::numeric_limits<float>::quiet_NaN();
        EXPECT_THROW(cache.get(changed),std::invalid_argument);EXPECT_EQ(storage.calls,4u);
    }

    TEST(TerrainVertexPreparation, UnusedVerticesExpireAndHeldVerticesSurvive)
    {
        Storage storage;Terrain::VertexBufferCache cache(storage,{});
        auto held=cache.get(key);cache.update(1,5);cache.update(20,5);
        EXPECT_EQ(cache.getStats().mSize,1u);held.reset();cache.update(30,5);
        EXPECT_EQ(cache.getStats().mSize,0u);cache.get(key);EXPECT_EQ(storage.calls,2u);
    }

    TEST(TerrainVertexPreparation, SharedArraysRetainDistinctStitchTopologyAndRayHits)
    {
        Storage storage;Terrain::VertexBufferCache cache(storage,{});Terrain::BufferCache indices;
        const auto vertices=cache.get(key);
        osg::ref_ptr<osg::Geometry> previous;
        for (const unsigned flags: {0u,1u,0x1111u})
        {
            osg::ref_ptr<osg::Geometry> geometry=new osg::Geometry;
            vertices->attach(*geometry);
            geometry->setUseDisplayList(false);geometry->setUseVertexBufferObjects(true);
            geometry->addPrimitiveSet(indices.getIndexBuffer(5,flags));
            geometry->setTexCoordArrayList(osg::Geometry::ArrayList(2,indices.getUVBuffer(5)));
            osg::ref_ptr<osg::KdTree> tree=new osg::KdTree;osg::KdTree::BuildOptions options;
            ASSERT_TRUE(tree->build(options,geometry));geometry->setShape(tree);
            if (previous)
            {
                EXPECT_EQ(previous->getVertexArray(),geometry->getVertexArray());
                EXPECT_NE(previous->getPrimitiveSet(0),geometry->getPrimitiveSet(0));
                EXPECT_NE(previous->getShape(),geometry->getShape());
            }
            for (unsigned i=0;i<45;++i)
            {
                const double x=.4+(i%9)*.52,y=.4+(i/9)*.91;
                const auto cast=[&](bool accelerated){
                    osg::ref_ptr<osgUtil::LineSegmentIntersector> ray=new osgUtil::LineSegmentIntersector(
                        osg::Vec3d(x,y,-5),osg::Vec3d(x+.02,y+.03,10));
                    osgUtil::IntersectionVisitor visitor(ray);visitor.setUseKdTreeWhenAvailable(accelerated);
                    geometry->accept(visitor);return ray;
                };
                const auto a=cast(false),b=cast(true);
                ASSERT_EQ(a->containsIntersections(),b->containsIntersections());
                if (a->containsIntersections())
                {
                    EXPECT_NEAR(a->getFirstIntersection().ratio,b->getFirstIntersection().ratio,1e-7);
                    EXPECT_EQ(a->getFirstIntersection().indexList,b->getFirstIntersection().indexList);
                }
            }
            previous=geometry;
        }
        EXPECT_EQ(storage.calls,1u);
        EXPECT_EQ(vertices->mPositions->getVertexBufferObject()->getNumBufferData(),3u);
    }
}
