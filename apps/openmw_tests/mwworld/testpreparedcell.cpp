#include <apps/openmw/mwclass/static.hpp>
#include <apps/openmw/mwphysics/object.hpp>
#include <apps/openmw/mwphysics/mtphysics.hpp>
#include <apps/openmw/mwphysics/actorconvexcallback.hpp>
#include <apps/openmw/mwphysics/projectileconvexcallback.hpp>
#include <apps/openmw/mwphysics/projectile.hpp>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <components/resource/bulletshape.hpp>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <apps/openmw/mwphysics/physicssystem.hpp>
#include <apps/openmw/mwrender/animation.hpp>
#include <apps/openmw/mwrender/objects.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/cellpreloader.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/preparedcell.hpp>

#include <components/esm3/readerscache.hpp>
#include <components/esm3/loadstat.hpp>
#include <LinearMath/btIDebugDraw.h>
#include <components/misc/convert.hpp>
#include <components/misc/combinedperformance.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/unrefqueue.hpp>
#include <components/sceneutil/userdata.hpp>
#include <components/sceneutil/compilepriority.hpp>
#include <components/settings/values.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/testing/util.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/filesystemarchive.hpp>

#include <osg/Geometry>
#include <osg/Material>
#include <osg/Fog>
#include <osg/GraphicsContext>
#include <osg/State>
#include <osgDB/WriteFile>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#ifdef OPENMW_TEST_PREPARED_CELLS_GL
#include "preparedcellgl.hpp"
#include <components/sceneutil/gameplaycompile.hpp>
#include <osgUtil/SceneView>
#include <osgUtil/Statistics>
#endif

namespace
{
    class PreparedTestContext final : public osg::GraphicsContext
    {
    public:
        PreparedTestContext() { setState(new osg::State); }
        ~PreparedTestContext() override { setState(nullptr); }
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

    class PreparedCellTest : public testing::Test
    {
    protected:
        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        ESM::Static mRecord;
        ESM::Cell mCellRecord;
        std::unique_ptr<MWWorld::CellStore> mCell;
        std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::Static>>> mReferences;
        VFS::Manager mVfs;
        ToUTF8::StatelessUtf8Encoder mEncoder{ ToUTF8::WINDOWS_1252 };
        Resource::ResourceSystem mResources{ &mVfs, 1.0, &mEncoder };
        Resource::BulletShapeManager mShapes{ &mVfs, mResources.getSceneManager(), mResources.getNifFileManager(), 1.0 };
        osg::ref_ptr<osg::Group> mRoot = new osg::Group;
        SceneUtil::UnrefQueue mUnref;
        MWRender::Objects mObjects{ &mResources, mRoot, mUnref };
        std::unique_ptr<MWPhysics::PhysicsSystem> mPhysics;
        const VFS::Path::Normalized mModel{ "meshes/prepared.osgt" };
        bool mOriginalDistant = Settings::terrain().mDistantTerrain;
        bool mOriginalPreloadEnabled = Settings::cells().mPreloadEnabled;

        void SetUp() override
        {
            const char* requested = std::getenv("OPENMW_PREPARED_CELLS");
            const bool expected = requested && std::string_view(requested) == "1";
            ASSERT_EQ(Misc::usePreparedCells(), expected);
            RecordProperty("prepared_cells_enabled", expected ? "true" : "false");
            Settings::terrain().mDistantTerrain.set(false);
            MWClass::Static::registerSelf();
            const auto directory = TestingOpenMW::currentTestDirPath();
            std::filesystem::create_directories(directory / "meshes");
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
            vertices->push_back({ -4, -2, 0 }); vertices->push_back({ 4, -2, 0 });
            vertices->push_back({ 4, 2, 0 }); vertices->push_back({ -4, 2, 0 });
            geometry->setVertexArray(vertices);
            const osg::Vec4 white(1, 1, 1, 1);
            geometry->setColorArray(new osg::Vec4Array(1, &white), osg::Array::BIND_OVERALL);
            osg::ref_ptr<osg::Material> material = new osg::Material;
            material->setEmission(osg::Material::FRONT_AND_BACK, { 1, 1, 1, 1 });
            geometry->getOrCreateStateSet()->setAttribute(material);
            geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLE_FAN, 0, 4));
            osg::ref_ptr<osg::Group> root = new osg::Group;
            root->addChild(geometry);
            root->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
            ASSERT_TRUE(osgDB::writeNodeFile(*root, (directory / "meshes/prepared.osgt").string()));
            mVfs.addArchive(std::make_unique<VFS::FileSystemArchive>(directory));
            mVfs.buildIndex();
            mResources.getSceneManager()->setShaderPath(std::filesystem::path(OPENMW_PROJECT_SOURCE_DIR) / "files/shaders");
            auto defines = Shader::getDefaultDefines();
            for (const auto& [name, value] : SceneUtil::ShadowManager::getShadowsDisabledDefines())
                defines[name] = value;
            osg::ref_ptr<SceneUtil::LightManager> lights = new SceneUtil::LightManager;
            for (const auto& [name, value] : lights->getLightDefines())
                defines[name] = value;
            mResources.getSceneManager()->getShaderManager().setGlobalDefines(defines);
            mRecord.blank();
            mRecord.mId = ESM::RefId::stringRefId("prepared static fixture");
            mRecord.mModel = "prepared.osgt";
            mCellRecord.blank();
            mCellRecord.updateId();
            mCell = std::make_unique<MWWorld::CellStore>(MWWorld::Cell(mCellRecord), mStore, mReaders);
            mPhysics = std::make_unique<MWPhysics::PhysicsSystem>(&mResources, nullptr);
        }

        MWWorld::Ptr reference(float x = 10)
        {
            ESM::CellRef ref;
            ref.blank();
            ref.mRefID = mRecord.mId;
            ref.mRefNum = { static_cast<std::uint32_t>(mReferences.size() + 1), 0 };
            ref.mCount = 1;
            ref.mScale = 1.5f;
            ref.mPos.pos[0] = x; ref.mPos.pos[1] = 20; ref.mPos.pos[2] = 30;
            mReferences.push_back(std::make_unique<MWWorld::LiveCellRef<ESM::Static>>(ref, &mRecord));
            return MWWorld::Ptr(mReferences.back().get(), mCell.get());
        }

        std::shared_ptr<MWWorld::PreparedCell> prepare(const std::vector<MWWorld::Ptr>& refs, bool rendering = true)
        {
            std::vector<MWWorld::StaticPreparationInput> inputs;
            for (const auto& ptr : refs)
                inputs.push_back(MWWorld::StaticPreparationInput::capture(ptr));
            // Exercise the production constructor on an actual worker, without any live world services.
            return std::async(std::launch::async, [&, inputs = std::move(inputs)] {
                const std::atomic_bool abort{ false };
                return MWWorld::PreparedCell::prepare(inputs, mResources, mShapes, true, abort, rendering);
            }).get();
        }

        void ready(MWWorld::PreparedCell& cell)
        {
            // Deliberately simulated graphics acknowledgement: these are CPU/ownership tests,
            // not GPU completion or latency claims. Production obtains this from ICO's callback.
            cell.setCompileCompletion(std::make_shared<std::atomic_bool>(true));
            cell.detachCompileRoot();
        }

        void publish(const MWWorld::Ptr& ptr, MWWorld::PreparedStaticObject object)
        {
            ASSERT_TRUE(object.mPrepared);
            ASSERT_TRUE(object.mRendering);
            mObjects.insertPreparedStatic(ptr, std::move(object.mRendering));
            mPhysics->addPreparedObject(ptr, std::move(object.mPhysics), MWPhysics::CollisionType_World);
        }

        auto ray(float x = 10, float y = 20) const
        {
            return mPhysics->castRay({ x, y, 50 }, { x, y, 10 });
        }

        void TearDown() override
        {
            for (const auto& ref : mReferences)
            {
                const MWWorld::Ptr ptr(ref.get(), mCell.get());
                mPhysics->remove(ptr);
                mObjects.removeObject(ptr);
            }
            mPhysics.reset();
            Settings::terrain().mDistantTerrain.set(mOriginalDistant);
            Settings::cells().mPreloadEnabled.set(mOriginalPreloadEnabled);
        }

        MWWorld::Ptr storedReference()
        {
            const auto source = reference();
            mCell->load();
            return MWWorld::Ptr(mCell->insert(source.get<ESM::Static>()), mCell.get());
        }

        static void drain(SceneUtil::WorkQueue& queue)
        {
            while (queue.getNumItems())
            {
                auto item = queue.removeWorkItem();
                item->doWork();
                item->signalDone();
            }
        }

        static void acknowledge(osgUtil::IncrementalCompileOperation& compile)
        {
            // Execute the real completion callbacks; never invoke graphics through a fake context.
            auto sets = compile.getToCompile();
            for (const auto& set : sets)
            {
                if (set->_compileCompletedCallback)
                    set->_compileCompletedCallback->compileCompleted(set);
                compile.remove(set);
            }
        }
    };

    TEST_F(PreparedCellTest, WorkerPreparationLeavesLiveRenderingPhysicsAndReferenceUntouched)
    {
        const auto ptr = reference();
        const auto packet = prepare({ ptr });
        ASSERT_EQ(packet->size(), 1u);
        EXPECT_FALSE(ptr.getRefData().getBaseNode());
        EXPECT_FALSE(ptr.getRefData().getCustomData());
        EXPECT_FALSE(ptr.getRefData().getLuaScripts());
        EXPECT_EQ(mRoot->getNumChildren(), 0u);
        EXPECT_FALSE(mObjects.getAnimation(ptr));
        EXPECT_FALSE(mPhysics->getObject(ptr));
        EXPECT_FALSE(ray().mHit);
        EXPECT_FALSE(packet->ready());
        EXPECT_THROW(packet->detachCompileRoot(), std::logic_error);
        EXPECT_FALSE(packet->take(ptr, mModel).mPrepared);
    }

    TEST_F(PreparedCellTest, OrderedPublicationBindsIdentityAndCollisionExactlyOnce)
    {
        const auto ptr = reference();
        const auto packet = prepare({ ptr });
        ready(*packet);
        publish(ptr, packet->take(ptr, mModel));
        ASSERT_TRUE(ptr.getRefData().getBaseNode());
        EXPECT_EQ(*SceneUtil::findUserData<MWWorld::Ptr>(*ptr.getRefData().getBaseNode()), ptr);
        EXPECT_EQ(mObjects.getAnimation(ptr)->getPtr(), ptr);
        ASSERT_TRUE(mPhysics->getObject(ptr));
        EXPECT_TRUE(mPhysics->getObject(ptr)->isActive());
        const auto hit = ray();
        EXPECT_TRUE(hit.mHit);
        EXPECT_EQ(hit.mHitObject, ptr);
        EXPECT_NEAR(hit.mHitPos.z(), 30.f, 0.001f);
        EXPECT_FALSE(packet->take(ptr, mModel).mPrepared);
        EXPECT_THROW(packet->detachCompileRoot(), std::logic_error);
        mPhysics->remove(ptr);
        mObjects.removeObject(ptr);
        EXPECT_FALSE(ray().mHit);
        EXPECT_FALSE(ptr.getRefData().getBaseNode());
    }

    TEST_F(PreparedCellTest, SameAssetProducesIndependentLiveObjects)
    {
        const auto a = reference(10), b = reference(40);
        const auto packet = prepare({ a, b });
        ready(*packet);
        publish(a, packet->take(a, mModel));
        publish(b, packet->take(b, mModel));
        EXPECT_NE(a.getRefData().getBaseNode(), b.getRefData().getBaseNode());
        EXPECT_NE(mPhysics->getObject(a)->getShapeInstance(), mPhysics->getObject(b)->getShapeInstance());
        EXPECT_EQ(ray(10).mHitObject, a);
        EXPECT_EQ(ray(40).mHitObject, b);
        mPhysics->remove(a);
        EXPECT_FALSE(ray(10).mHit);
        EXPECT_EQ(ray(40).mHitObject, b);
    }

    TEST_F(PreparedCellTest, PreparedAndOrdinaryPublicationHaveEquivalentTransformsAndHits)
    {
        const auto ptr = reference();
        auto position = ptr.getRefData().getPosition();
        position.rot[0] = 0.2f;
        position.rot[1] = 0.3f;
        position.rot[2] = 0.5f;
        ptr.getRefData().setPosition(position);
        const auto packet = prepare({ ptr });
        ready(*packet);
        publish(ptr, packet->take(ptr, mModel));
        const auto transform = mPhysics->getObject(ptr)->getTransform();
        const auto hit = ray();
        const auto renderedPosition = ptr.getRefData().getBaseNode()->getPosition();
        const auto renderedScale = ptr.getRefData().getBaseNode()->getScale();
        const auto renderedAttitude = ptr.getRefData().getBaseNode()->getAttitude();
        mPhysics->remove(ptr);
        mObjects.removeObject(ptr);
        mObjects.insertModel(ptr, std::string(mModel.value()));
        // Scene::addObject applies this rotation after ordinary static rendering insertion.
        ptr.getRefData().getBaseNode()->setAttitude(Misc::Convert::makeOsgQuat(position));
        mPhysics->addObject(ptr, mModel, Misc::Convert::makeOsgQuat(position), MWPhysics::CollisionType_World);
        EXPECT_EQ(mPhysics->getObject(ptr)->getTransform(), transform);
        EXPECT_EQ(ray().mHitObject, hit.mHitObject);
        EXPECT_EQ(ray().mHitPos, hit.mHitPos);
        EXPECT_EQ(ptr.getRefData().getBaseNode()->getPosition(), renderedPosition);
        EXPECT_EQ(ptr.getRefData().getBaseNode()->getScale(), renderedScale);
        EXPECT_EQ(ptr.getRefData().getBaseNode()->getAttitude(), renderedAttitude);
    }

    TEST_F(PreparedCellTest, ChangedPositionScaleEnableCountAndModelRejectStaleInstances)
    {
        const auto ptr = reference();
        const auto packet = prepare({ ptr });
        ready(*packet);
        const auto original = ptr.getRefData().getPosition();
        auto changed = original; changed.pos[0] += 1;
        ptr.getRefData().setPosition(changed);
        EXPECT_FALSE(packet->take(ptr, mModel).mPrepared);
        ptr.getRefData().setPosition(original);
        ptr.getCellRef().setScale(2);
        EXPECT_FALSE(packet->take(ptr, mModel).mPrepared);
        ptr.getCellRef().setScale(1.5f);
        ptr.getRefData().disable();
        EXPECT_FALSE(packet->take(ptr, mModel).mPrepared);
        ptr.getRefData().enable();
        const auto savedRef = ptr.getCellRef();
        auto deleted = ESM::makeBlankCellRef();
        deleted.mRefNum = savedRef.getRefNum();
        deleted.mRefID = savedRef.getRefId();
        deleted.mScale = savedRef.getScale();
        deleted.mPos = original;
        deleted.mCount = 0;
        // Supply a deleted saved reference without invoking the live world's script-removal service.
        ptr.getCellRef() = MWWorld::CellRef(deleted);
        EXPECT_FALSE(packet->take(ptr, mModel).mPrepared);
        ptr.getCellRef() = savedRef;
        EXPECT_FALSE(packet->take(ptr, VFS::Path::NormalizedView("meshes/changed.osgt")).mPrepared);
        EXPECT_TRUE(packet->take(ptr, mModel).mPrepared);
        EXPECT_FALSE(packet->take(MWWorld::Ptr{}, mModel).mPrepared);
    }

    TEST_F(PreparedCellTest, UnpublishedDiscardAndCancelledPreparationNeverCreateLiveObjects)
    {
        const auto ptr = reference();
        auto packet = prepare({ ptr });
        packet.reset();
        EXPECT_FALSE(ray().mHit);
        EXPECT_EQ(mRoot->getNumChildren(), 0u);
        const std::atomic_bool abort{ true };
        packet = MWWorld::PreparedCell::prepare({ MWWorld::StaticPreparationInput::capture(ptr) },
            mResources, mShapes, false, abort);
        EXPECT_EQ(packet->size(), 0u);
        EXPECT_FALSE(ptr.getRefData().getBaseNode());
    }

    TEST_F(PreparedCellTest, PagedRenderingConsumesOnlyCollisionAndNeverPublishesIndividualNode)
    {
        const auto ptr = reference();
        const auto packet = prepare({ ptr }, false);
        packet->setCompileCompletion(std::make_shared<std::atomic_bool>(true));
        EXPECT_EQ(packet->compileRoot()->getNumChildren(), 0u);
        ready(*packet);
        auto object = packet->take(ptr, mModel, false);
        EXPECT_TRUE(object.mPrepared);
        EXPECT_FALSE(object.mRendering);
        mPhysics->addPreparedObject(ptr, std::move(object.mPhysics), MWPhysics::CollisionType_World);
        EXPECT_TRUE(ray().mHit);
        EXPECT_FALSE(ptr.getRefData().getBaseNode());
        EXPECT_EQ(mRoot->getNumChildren(), 0u);
        EXPECT_FALSE(packet->take(ptr, mModel).mPrepared);
        EXPECT_EQ(packet->collisionOnlyUses(), 1u);
        EXPECT_EQ(packet->renderedUses(), 0u);
        EXPECT_EQ(packet->unused(), 0u);
    }

    TEST_F(PreparedCellTest, PostponedPhysicsDoesNotLeakPreparedCollisionIntoWorld)
    {
        const auto ptr = reference();
        const auto packet = prepare({ ptr });
        ready(*packet);
        ptr.getRefData().mPhysicsPostponed = true;
        auto object = packet->take(ptr, mModel, false);
        mPhysics->addPreparedObject(ptr, std::move(object.mPhysics), MWPhysics::CollisionType_World);
        EXPECT_FALSE(ray().mHit);
        EXPECT_FALSE(mPhysics->getObject(ptr));
    }

    TEST_F(PreparedCellTest, DuplicatePublicationRejectionPreservesTheExistingObject)
    {
        const auto ptr = reference();
        const auto first = prepare({ ptr }), duplicate = prepare({ ptr });
        ready(*first); ready(*duplicate);
        publish(ptr, first->take(ptr, mModel));
        auto second = duplicate->take(ptr, mModel);
        auto* originalNode = ptr.getRefData().getBaseNode();
        const auto* originalPhysics = mPhysics->getObject(ptr);
        EXPECT_THROW(mObjects.insertPreparedStatic(ptr, std::move(second.mRendering)), std::logic_error);
        EXPECT_THROW(mPhysics->addPreparedObject(ptr, std::move(second.mPhysics), MWPhysics::CollisionType_World),
            std::logic_error);
        EXPECT_EQ(ptr.getRefData().getBaseNode(), originalNode);
        EXPECT_EQ(mPhysics->getObject(ptr), originalPhysics);
        EXPECT_EQ(ray().mHitObject, ptr);
        EXPECT_EQ(mObjects.getAnimation(ptr)->getPtr(), ptr);
    }

    TEST_F(PreparedCellTest, DormantBroadphaseIsInvisibleAndPublicationUpdatesImmediateQueries)
    {
        const auto ptr = reference();
        auto packet = prepare({ ptr });
        packet->setCompileCompletion(std::make_shared<std::atomic_bool>(true));
        EXPECT_EQ(packet->stagePhysics(*mPhysics, std::chrono::steady_clock::time_point::max()), 1u);
        ASSERT_TRUE(packet->physicsStaged());
        EXPECT_EQ(mPhysics->stagedObjectCount(), 1u);
        EXPECT_FALSE(mPhysics->getObject(ptr));
        EXPECT_FALSE(ptr.getRefData().getBaseNode());
        EXPECT_FALSE(ray().mHit);
        EXPECT_FALSE(mPhysics->castSphere({ 10,20,50 }, { 10,20,10 }, 1).mHit);
        EXPECT_FALSE(mPhysics->isAreaOccupiedByOtherActor(nullptr, { 10,20,30 }, 10));

        const auto probe = reference();
        auto shape = std::make_shared<Resource::BulletShape>();
        shape->mCollisionShape.reset(new btBoxShape(btVector3(1,1,1)));
        auto probeObject = std::make_shared<MWPhysics::Object>(Resource::makeInstance(shape),
            osg::Vec3f(10,20,30), osg::Quat(), 1);
        mPhysics->addPreparedObject(probe, probeObject, MWPhysics::CollisionType_Actor);
        EXPECT_TRUE(mPhysics->getCollisionsPoints(probe, MWPhysics::CollisionType_Actor,
            MWPhysics::CollisionType_World).empty());
        packet->detachCompileRoot();
        auto object = packet->take(ptr, mModel);
        const auto physicsObject = object.mPhysics;
        ASSERT_TRUE(physicsObject->isStaged());
        EXPECT_TRUE(physicsObject->getPtr().isEmpty());
        publish(ptr, std::move(object));
        EXPECT_TRUE(physicsObject->isActive());
        EXPECT_FALSE(physicsObject->isStaged());
        EXPECT_EQ(mPhysics->stagedObjectCount(), 0u);
        EXPECT_EQ(physicsObject->getPtr(), ptr);
        const auto contacts = mPhysics->getCollisionsPoints(probe, MWPhysics::CollisionType_Actor,
            MWPhysics::CollisionType_World);
        ASSERT_FALSE(contacts.empty());
        mPhysics->remove(probe);
        probeObject.reset();
        EXPECT_EQ(ray().mHitObject, ptr);
        EXPECT_EQ(mPhysics->castSphere({ 10,20,50 }, { 10,20,10 }, 1).mHitObject, ptr);
        EXPECT_EQ(physicsObject->getCollisionObject()->getCollisionFlags()
            & btCollisionObject::CF_DISABLE_VISUALIZE_OBJECT, 0);
    }

    TEST_F(PreparedCellTest, DormantAabbAndDebugDrawingStayHiddenUntilPublication)
    {
        struct Drawer : btIDebugDraw
        {
            int mLines = 0, mMode = 0;
            void drawLine(const btVector3&,const btVector3&,const btVector3&) override { ++mLines; }
            void drawContactPoint(const btVector3&,const btVector3&,btScalar,int,const btVector3&) override {}
            void reportErrorWarning(const char*) override {}
            void draw3dText(const btVector3&,const char*) override {}
            void setDebugMode(int mode) override { mMode = mode; }
            int getDebugMode() const override { return mMode; }
        };
        auto drawer = std::make_unique<Drawer>();
        auto* observed = drawer.get();
        mPhysics = std::make_unique<MWPhysics::PhysicsSystem>(&mResources,std::move(drawer));
        ASSERT_TRUE(mPhysics->toggleDebugRendering());
        const auto ptr = reference();
        auto packet = prepare({ptr},false);
        packet->setCompileCompletion(std::make_shared<std::atomic_bool>(true));
        ASSERT_EQ(packet->stagePhysics(*mPhysics,std::chrono::steady_clock::time_point::max()),1u);
        mPhysics->debugDraw();
        EXPECT_EQ(observed->mLines,0);
        EXPECT_FALSE(mPhysics->isAreaOccupiedByOtherActor(nullptr,{10,20,30},10));
        packet->detachCompileRoot();
        auto object = packet->take(ptr,mModel,false);
        // A focused Actor-group probe makes the real AABB query's mask relevant.
        mPhysics->addPreparedObject(ptr,std::move(object.mPhysics),MWPhysics::CollisionType_Actor);
        EXPECT_TRUE(mPhysics->isAreaOccupiedByOtherActor(nullptr,{10,20,30},10));
        mPhysics->debugDraw();
        EXPECT_GT(observed->mLines,0);
    }

    TEST_F(PreparedCellTest, ActorAndProjectileSweepsSeeOnlyPublishedCollision)
    {
        btDefaultCollisionConfiguration configuration;
        btCollisionDispatcher dispatcher(&configuration);
        btDbvtBroadphase broadphase;
        btCollisionWorld world(&dispatcher,&broadphase,&configuration);
        MWPhysics::PhysicsTaskScheduler scheduler(1.f/60.f,&world);
        const auto ptr = reference();
        auto object = std::make_shared<MWPhysics::Object>(mShapes.getInstance(mModel,1.5f),
            osg::Vec3f(10,20,30),osg::Quat(),1.5f);
        ASSERT_TRUE(object->stage(&scheduler));
        btSphereShape sphere(1);
        const btVector3 from(10,20,50), to(10,20,10);
        btCollisionObject actor;
        actor.setCollisionShape(&sphere);
        actor.setWorldTransform(btTransform(btQuaternion::getIdentity(),from));
        const auto actorSweep = [&] {
            MWPhysics::ActorConvexCallback callback(&actor,from-to,0,&world);
            callback.m_collisionFilterGroup = MWPhysics::CollisionType_Actor;
            callback.m_collisionFilterMask = MWPhysics::CollisionType_World;
            scheduler.convexSweepTest(&sphere,btTransform(btQuaternion::getIdentity(),from),
                btTransform(btQuaternion::getIdentity(),to),callback);
            return callback.hasHit();
        };
        MWPhysics::Projectile projectile({}, {1000,1000,1000},1,&scheduler,mPhysics.get());
        const auto projectileSweep = [&] {
            MWPhysics::ProjectileConvexCallback callback(nullptr,projectile.getCollisionObject(),from,to,projectile);
            callback.m_collisionFilterGroup = MWPhysics::CollisionType_Projectile;
            callback.m_collisionFilterMask = MWPhysics::CollisionType_World;
            scheduler.convexSweepTest(&sphere,btTransform(btQuaternion::getIdentity(),from),
                btTransform(btQuaternion::getIdentity(),to),callback);
            return callback.hasHit();
        };
        EXPECT_FALSE(actorSweep());
        EXPECT_FALSE(projectileSweep());
        EXPECT_TRUE(projectile.isActive());
        object->activate(ptr,MWPhysics::CollisionType_World,&scheduler);
        EXPECT_TRUE(actorSweep());
        EXPECT_TRUE(projectileSweep());
        EXPECT_FALSE(projectile.isActive());
        EXPECT_EQ(projectile.getTarget(),ptr);
    }

    TEST_F(PreparedCellTest, StagingYieldsToBusyPhysicsQueryWithoutWaiting)
    {
        btDefaultCollisionConfiguration configuration;
        btCollisionDispatcher dispatcher(&configuration);
        btDbvtBroadphase broadphase;
        btCollisionWorld world(&dispatcher,&broadphase,&configuration);
        MWPhysics::PhysicsTaskScheduler scheduler(1.f/60.f,&world);
        const auto ptr = reference();
        auto anchor = std::make_shared<MWPhysics::Object>(ptr,mShapes.getInstance(mModel,1.5f),
            osg::Quat(),MWPhysics::CollisionType_World,&scheduler);
        auto candidate = std::make_shared<MWPhysics::Object>(mShapes.getInstance(mModel,1.5f),
            osg::Vec3f(100,20,30),osg::Quat(),1.5f);
        struct BlockingRay : btCollisionWorld::ClosestRayResultCallback
        {
            std::promise<void> mStarted;
            std::shared_future<void> mRelease;
            bool mTimedOut = false, mNotified = false;
            explicit BlockingRay(std::shared_future<void> release)
                : ClosestRayResultCallback({10,20,50},{10,20,10}),mRelease(std::move(release)) {}
            btScalar addSingleResult(btCollisionWorld::LocalRayResult& hit,bool worldNormal) override
            {
                if (!std::exchange(mNotified,true)) mStarted.set_value();
                mTimedOut = mRelease.wait_for(std::chrono::seconds(2)) != std::future_status::ready;
                return ClosestRayResultCallback::addSingleResult(hit,worldNormal);
            }
        };
        std::promise<void> release;
        BlockingRay callback(release.get_future().share());
        callback.m_collisionFilterGroup = MWPhysics::CollisionType_Actor;
        callback.m_collisionFilterMask = MWPhysics::CollisionType_World;
        auto started = callback.mStarted.get_future();
        auto query = std::async(std::launch::async,[&] { scheduler.rayTest({10,20,50},{10,20,10},callback); });
        EXPECT_EQ(started.wait_for(std::chrono::seconds(2)),std::future_status::ready);
        const auto before = std::chrono::steady_clock::now();
        EXPECT_FALSE(candidate->stage(&scheduler));
        EXPECT_LT(std::chrono::steady_clock::now()-before,std::chrono::milliseconds(250));
        release.set_value(); query.get();
        EXPECT_FALSE(callback.mTimedOut);
        EXPECT_FALSE(candidate->isStaged());
        EXPECT_TRUE(candidate->stage(&scheduler));
        EXPECT_TRUE(candidate->unstage(false));
    }

    TEST_F(PreparedCellTest, StagingHonoursReadinessDeadlineAndObjectLimit)
    {
        const auto a = reference(), b = reference(100);
        auto packet = prepare({ a,b });
        const auto forever = std::chrono::steady_clock::time_point::max();
        EXPECT_EQ(packet->stagePhysics(*mPhysics, forever), 0u);
        packet->setCompileCompletion(std::make_shared<std::atomic_bool>(true));
        EXPECT_EQ(packet->stagePhysics(*mPhysics, std::chrono::steady_clock::time_point::min()), 0u);
        EXPECT_EQ(packet->stagePhysics(*mPhysics, forever, 0), 0u);
        EXPECT_EQ(packet->stagePhysics(*mPhysics, forever, 1), 1u);
        EXPECT_FALSE(packet->physicsStaged());
        EXPECT_EQ(mPhysics->stagedObjectCount(), 1u);
        EXPECT_FALSE(ray().mHit);
        EXPECT_EQ(packet->stagePhysics(*mPhysics, forever, 1), 1u);
        EXPECT_TRUE(packet->physicsStaged());
        EXPECT_EQ(packet->stagePhysics(*mPhysics, forever), 0u);
        packet->detachCompileRoot();
        publish(a, packet->take(a,mModel));
        publish(b, packet->take(b,mModel));
        EXPECT_EQ(ray().mHitObject, a);
        EXPECT_EQ(ray(100).mHitObject, b);
    }

    TEST_F(PreparedCellTest, CancelledStagedObjectsRetireWithoutBecomingQueryable)
    {
        const auto ptr = reference();
        auto packet = prepare({ ptr }, false);
        packet->setCompileCompletion(std::make_shared<std::atomic_bool>(true));
        auto credit = std::make_shared<int>(42);
        const std::weak_ptr<int> weakCredit = credit;
        packet->retainPreparationCharge(credit);
        credit.reset();
        ASSERT_EQ(packet->stagePhysics(*mPhysics, std::chrono::steady_clock::time_point::max()), 1u);
        packet.reset();
        EXPECT_FALSE(weakCredit.expired());
        EXPECT_FALSE(ray().mHit);
        SceneUtil::WorkQueue retirement(0);
        mPhysics->retirePreparedObjects(retirement, std::chrono::steady_clock::time_point::max());
        EXPECT_EQ(mPhysics->stagedObjectCount(), 0u);
        EXPECT_FALSE(weakCredit.expired());
        EXPECT_FALSE(ray().mHit);
        drain(retirement);
        EXPECT_TRUE(weakCredit.expired());
    }

    TEST_F(PreparedCellTest, PreparationCreditOutlivesRetainedCollisionAndGraphDestructors)
    {
        auto credit = std::make_shared<int>(42);
        const std::weak_ptr<int> weak = credit;
        bool shapeDestroyed = false, chargedAtDestruction = false;
        auto shape = std::shared_ptr<Resource::BulletShape>(new Resource::BulletShape,
            [&](auto* source) {
                chargedAtDestruction = !weak.expired();
                delete source;
                shapeDestroyed = true;
            });
        shape->mCollisionShape.reset(new btBoxShape(btVector3(1,1,1)));
        auto object = std::make_shared<MWPhysics::Object>(Resource::makeInstance(shape),osg::Vec3f(),osg::Quat(),1);
        object->retainPreparationCharge(credit);
        shape.reset(); credit.reset(); object.reset();
        EXPECT_TRUE(shapeDestroyed);
        EXPECT_TRUE(chargedAtDestruction);
        EXPECT_TRUE(weak.expired());

        struct ObservedNode : osg::Group
        {
            std::weak_ptr<int> mCredit;
            bool& mCharged;
            ObservedNode(std::weak_ptr<int> token, bool& charged) : mCredit(token),mCharged(charged) {}
            ~ObservedNode() override { mCharged = !mCredit.expired(); }
        };
        auto packet = prepare({ reference() });
        credit = std::make_shared<int>(7);
        bool graphCharged = false;
        packet->compileRoot()->addChild(new ObservedNode(credit,graphCharged));
        packet->retainPreparationCharge(credit);
        credit.reset(); packet.reset();
        EXPECT_TRUE(graphCharged);
    }

    TEST_F(PreparedCellTest, PhysicsTeardownDetachesExternallyRetainedDormantObjects)
    {
        const auto ptr = reference();
        auto packet = prepare({ ptr }, false);
        packet->setCompileCompletion(std::make_shared<std::atomic_bool>(true));
        ASSERT_EQ(packet->stagePhysics(*mPhysics, std::chrono::steady_clock::time_point::max()), 1u);
        packet->detachCompileRoot();
        auto object = packet->take(ptr,mModel,false);
        ASSERT_TRUE(object.mPhysics->isStaged());
        mPhysics.reset();
        EXPECT_FALSE(object.mPhysics->isStaged());
        EXPECT_FALSE(object.mPhysics->isActive());
        EXPECT_EQ(object.mPhysics->getCollisionObject()->getBroadphaseHandle(), nullptr);
        object = {};
        mPhysics = std::make_unique<MWPhysics::PhysicsSystem>(&mResources, nullptr);
    }

    TEST_F(PreparedCellTest, StaleStagedReferenceFallsBackAndNeverExposesOldCollision)
    {
        const auto ptr = reference();
        auto packet = prepare({ ptr }, false);
        packet->setCompileCompletion(std::make_shared<std::atomic_bool>(true));
        ASSERT_EQ(packet->stagePhysics(*mPhysics, std::chrono::steady_clock::time_point::max()), 1u);
        packet->detachCompileRoot();
        auto position = ptr.getRefData().getPosition();
        position.pos[0] = 100;
        ptr.getRefData().setPosition(position);
        EXPECT_FALSE(packet->take(ptr,mModel,false).mPrepared);
        EXPECT_FALSE(ray().mHit);
        mPhysics->addObject(ptr,mModel,osg::Quat(),MWPhysics::CollisionType_World);
        EXPECT_FALSE(ray().mHit);
        EXPECT_EQ(ray(100).mHitObject, ptr);
        packet.reset();
        SceneUtil::WorkQueue retirement(0);
        mPhysics->retirePreparedObjects(retirement, std::chrono::steady_clock::time_point::max());
        drain(retirement);
        EXPECT_EQ(mPhysics->stagedObjectCount(), 0u);
        EXPECT_EQ(ray(100).mHitObject, ptr);
    }

    TEST_F(PreparedCellTest, ManagerRequiresCompleteStagingAndClearRemovesDormantHandles)
    {
        const auto ptr = storedReference();
        for (int i=0; i<80; ++i)
        {
            const auto ref = reference(100+i*20);
            mCell->insert(ref.get<ESM::Static>());
        }
        auto queue = std::make_shared<SceneUtil::WorkQueue>(0);
        MWWorld::CellPreloader loader(&mResources,&mShapes,nullptr,nullptr,mPhysics.get());
        loader.setWorkQueue(queue);
        loader.setMaxCacheSize(8);
        loader.setPreloadInstances(false);
        loader.prepareCell(*mCell,0);
        drain(*queue);
        EXPECT_FALSE(loader.takePreparedCell(*mCell,0));
        loader.stagePreparedCells();
        if (!Misc::usePreparedCells())
        {
            EXPECT_EQ(mPhysics->stagedObjectCount(),0u);
            return;
        }
        EXPECT_GT(mPhysics->stagedObjectCount(),0u);
        EXPECT_LE(mPhysics->stagedObjectCount(),64u);
        EXPECT_FALSE(loader.takePreparedCell(*mCell,0));
        for (int i=0; i<100 && mPhysics->stagedObjectCount()<81; ++i) loader.stagePreparedCells();
        EXPECT_FALSE(ray().mHit);
        auto packet = loader.takePreparedCell(*mCell,0);
        ASSERT_TRUE(packet);
        EXPECT_TRUE(packet->physicsStaged());
        loader.clear();
        EXPECT_EQ(mPhysics->stagedObjectCount(),0u);
        auto object = packet->take(ptr,mModel,false);
        ASSERT_TRUE(object.mPhysics);
        EXPECT_FALSE(object.mPhysics->isStaged());
        EXPECT_FALSE(ray().mHit);
        drain(*queue);
    }

    TEST_F(PreparedCellTest, ManagerInvalidationCancelsStagedPacketsAndQueueReplacementUnstages)
    {
        storedReference();
        auto queue = std::make_shared<SceneUtil::WorkQueue>(0);
        MWWorld::CellPreloader loader(&mResources,&mShapes,nullptr,nullptr,mPhysics.get());
        loader.setWorkQueue(queue); loader.setMaxCacheSize(8); loader.setPreloadInstances(false);
        loader.prepareCell(*mCell,0); drain(*queue); loader.stagePreparedCells();
        if (!Misc::usePreparedCells()) return;
        ASSERT_EQ(mPhysics->stagedObjectCount(),1u);
        mVfs.buildIndex();
        loader.stagePreparedCells();
        EXPECT_FALSE(loader.takePreparedCell(*mCell,0));
        loader.updateCache(1); drain(*queue); loader.stagePreparedCells(); drain(*queue);
        EXPECT_EQ(mPhysics->stagedObjectCount(),0u);
        loader.prepareCell(*mCell,2); drain(*queue); loader.stagePreparedCells();
        ASSERT_EQ(mPhysics->stagedObjectCount(),1u);
        loader.setPreparedWorkQueue(std::make_shared<SceneUtil::WorkQueue>(0));
        EXPECT_EQ(mPhysics->stagedObjectCount(),0u);
        EXPECT_FALSE(ray().mHit);
        drain(*queue);
    }

    class PreparedCellBenchmark : public PreparedCellTest, public testing::WithParamInterface<std::tuple<bool, bool>> {};

    TEST_P(PreparedCellBenchmark, DensePublicationMeasuresPreparedAndOrdinaryOwnerCost)
    {
        const auto [rendering, grid] = GetParam();
        constexpr std::size_t count = 1024, rounds = 12;
        std::vector<MWWorld::Ptr> refs;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto ptr = reference(10 + (grid ? i % 32 : i) * 20);
            if (grid)
            {
                auto position = ptr.getRefData().getPosition();
                position.pos[1] += (i / 32) * 20;
                ptr.getRefData().setPosition(position);
            }
            refs.push_back(ptr);
        }
        const auto last = refs.back().getRefData().getPosition();
        std::vector<double> preparedTimes, stagedTimes, stagingTimes, stagingSlices, ordinaryTimes, constructionTimes;
        std::vector<double> emptyQueryTimes, dormantQueryTimes;
        const auto measure = [](auto&& work) {
            const auto started = std::chrono::steady_clock::now();
            work();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        };
        const auto remove = [&] {
            for (const auto& ptr : refs)
            {
                mPhysics->remove(ptr);
                mObjects.removeObject(ptr);
            }
            // Complete retirement outside the measurement; do not retain prior rounds' graphs.
            SceneUtil::WorkQueue retirement(0);
            mUnref.flush(retirement);
            drain(retirement);
        };
        const auto ordinary = [&] {
            ordinaryTimes.push_back(measure([&] {
                for (const auto& ptr : refs)
                {
                    if (rendering)
                        mObjects.insertModel(ptr, std::string(mModel.value()));
                    mPhysics->addObject(ptr, mModel, Misc::Convert::makeOsgQuat(ptr.getRefData().getPosition()),
                        MWPhysics::CollisionType_World);
                }
            }));
            EXPECT_EQ(ray(10).mHitObject, refs.front());
            EXPECT_EQ(ray(last.pos[0], last.pos[1]).mHitObject, refs.back());
            remove();
        };
        const auto prepared = [&](bool staged) {
            std::shared_ptr<MWWorld::PreparedCell> packet;
            constructionTimes.push_back(measure([&] { packet = prepare(refs, rendering); }));
            ASSERT_EQ(packet->size(), count);
            packet->setCompileCompletion(std::make_shared<std::atomic_bool>(true));
            const auto queries = [&] {
                for (std::size_t i=0; i<256; ++i)
                {
                    const auto& pos = refs[(i*17)%refs.size()].getRefData().getPosition();
                    EXPECT_FALSE(mPhysics->castSphere({pos.pos[0],pos.pos[1],50},{pos.pos[0],pos.pos[1],10},1).mHit);
                }
            };
            if (staged)
            {
                emptyQueryTimes.push_back(measure(queries));
                double total = 0;
                std::size_t slices = 0;
                while (!packet->physicsStaged())
                {
                    ASSERT_FALSE(packet->stagingFailed());
                    ASSERT_LT(++slices,10000u);
                    const double elapsed = measure([&] {
                        packet->stagePhysics(*mPhysics, std::chrono::steady_clock::now() + std::chrono::milliseconds(1), 64);
                    });
                    stagingSlices.push_back(elapsed); total += elapsed;
                }
                stagingTimes.push_back(total);
                dormantQueryTimes.push_back(measure(queries));
                EXPECT_FALSE(ray().mHit);
            }
            packet->detachCompileRoot();
            (staged ? stagedTimes : preparedTimes).push_back(measure([&] {
                for (const auto& ptr : refs)
                {
                    auto object = packet->take(ptr, mModel, rendering);
                    if (rendering)
                        mObjects.insertPreparedStatic(ptr, std::move(object.mRendering));
                    mPhysics->addPreparedObject(ptr, std::move(object.mPhysics), MWPhysics::CollisionType_World);
                }
            }));
            EXPECT_EQ(ray(10).mHitObject, refs.front());
            EXPECT_EQ(ray(last.pos[0], last.pos[1]).mHitObject, refs.back());
            EXPECT_EQ(packet->unused(), 0u);
            EXPECT_EQ(packet->renderedUses(), rendering ? count : 0u);
            EXPECT_EQ(packet->collisionOnlyUses(), rendering ? 0u : count);
            remove();
        };
        // Alternate order to reduce cache/order bias. This remains a synthetic CPU benchmark.
        for (std::size_t round = 0; round < rounds; ++round)
        {
            switch (round % 3)
            {
                case 0: ordinary(); prepared(false); prepared(true); break;
                case 1: prepared(false); prepared(true); ordinary(); break;
                case 2: prepared(true); ordinary(); prepared(false); break;
            }
        }
        const auto median = [](auto values) {
            std::sort(values.begin(), values.end());
            return (values[values.size() / 2 - 1] + values[values.size() / 2]) / 2;
        };
        const auto samples = [](const auto& values) {
            std::string result;
            for (const auto v : values)
            {
                if (!result.empty()) result += ',';
                result += std::to_string(v);
            }
            return result;
        };
        RecordProperty("prepared_static_count", count);
        RecordProperty("rounds", rounds);
        RecordProperty("individual_rendering", rendering ? "true" : "false");
        RecordProperty("layout", grid ? "32x32 grid" : "sorted line stress");
        RecordProperty("query_batch_size",256);
        RecordProperty("empty_query_samples_ms",samples(emptyQueryTimes));
        RecordProperty("dormant_query_samples_ms",samples(dormantQueryTimes));
        RecordProperty("empty_query_median_ms",std::to_string(median(emptyQueryTimes)));
        RecordProperty("dormant_query_median_ms",std::to_string(median(dormantQueryTimes)));
        RecordProperty("staged_owner_median_ms", std::to_string(median(stagedTimes)));
        RecordProperty("staging_total_median_ms", std::to_string(median(stagingTimes)));
        RecordProperty("staging_slice_max_ms", std::to_string(*std::max_element(stagingSlices.begin(), stagingSlices.end())));
        RecordProperty("staged_owner_samples_ms", samples(stagedTimes));
        RecordProperty("staging_total_samples_ms", samples(stagingTimes));
        RecordProperty("staging_slice_samples_ms", samples(stagingSlices));
        RecordProperty("prepared_owner_median_ms", std::to_string(median(preparedTimes)));
        RecordProperty("ordinary_owner_median_ms", std::to_string(median(ordinaryTimes)));
        RecordProperty("background_construction_median_ms", std::to_string(median(constructionTimes)));
        RecordProperty("prepared_owner_max_ms", std::to_string(*std::max_element(preparedTimes.begin(), preparedTimes.end())));
        RecordProperty("prepared_owner_samples_ms", samples(preparedTimes));
        RecordProperty("ordinary_owner_samples_ms", samples(ordinaryTimes));
        RecordProperty("background_samples_ms", samples(constructionTimes));
        std::cout << "Synthetic static CPU benchmark: objects=" << count << " individualRendering=" << rendering
            << " grid=" << grid << " preparedOwnerMedianMs=" << median(preparedTimes) << " ordinaryOwnerMedianMs=" << median(ordinaryTimes)
            << " stagedOwnerMedianMs=" << median(stagedTimes) << " stageSliceMaxMs=" << *std::max_element(stagingSlices.begin(), stagingSlices.end())
            << " backgroundMedianMs=" << median(constructionTimes)
            << " (excludes GPU, scripts, navigation and full cell activation)\n";
    }

    INSTANTIATE_TEST_SUITE_P(RenderModes, PreparedCellBenchmark, testing::Combine(testing::Bool(), testing::Bool()));

    TEST_F(PreparedCellTest, ManagerHonoursOptInAndRequiresGraphicsAcknowledgement)
    {
        const auto ptr = storedReference();
        auto queue = std::make_shared<SceneUtil::WorkQueue>(0);
        osg::ref_ptr<PreparedTestContext> context = new PreparedTestContext;
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> compile = new osgUtil::IncrementalCompileOperation;
        compile->getContextSet().insert(context);
        mResources.getSceneManager()->setIncrementalCompileOperation(compile);
        MWWorld::CellPreloader loader(&mResources, &mShapes, nullptr, nullptr);
        loader.setWorkQueue(queue);
        loader.setMaxCacheSize(8);
        loader.prepareCell(*mCell, 0);
        if (!Misc::usePreparedCells())
        {
            EXPECT_EQ(queue->getNumItems(), 0u);
            EXPECT_FALSE(loader.takePreparedCell(*mCell, 0));
            return;
        }
        ASSERT_EQ(queue->getNumItems(), 1u);
        drain(*queue);
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 0));
        EXPECT_FALSE(ptr.getRefData().getBaseNode());
        acknowledge(*compile);
        auto packet = loader.takePreparedCell(*mCell, 0);
        ASSERT_TRUE(packet);
        EXPECT_TRUE(packet->take(ptr, mModel).mPrepared);
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 0));
    }

    TEST_F(PreparedCellTest, PagedManagerPreparesCollisionWithoutGraphicsOrDuplicateRenderGraphs)
    {
        const auto ptr = storedReference();
        Settings::terrain().mDistantTerrain.set(true);
        ASSERT_TRUE(Settings::terrain().mObjectPaging);
        ASSERT_TRUE(Settings::terrain().mObjectPagingActiveGrid);
        auto queue = std::make_shared<SceneUtil::WorkQueue>(0);
        MWWorld::CellPreloader loader(&mResources, &mShapes, nullptr, nullptr);
        loader.setWorkQueue(queue);
        loader.setMaxCacheSize(8);
        loader.prepareCell(*mCell, 0);
        drain(*queue);
        auto packet = loader.takePreparedCell(*mCell, 0);
        if (!Misc::usePreparedCells())
        {
            EXPECT_FALSE(packet);
            return;
        }
        ASSERT_TRUE(packet);
        EXPECT_EQ(packet->compileRoot()->getNumChildren(), 0u);
        // If paging excludes the reference at handover, ordinary rendering remains possible.
        auto object = packet->take(ptr, mModel, true);
        EXPECT_TRUE(object.mPrepared);
        EXPECT_FALSE(object.mRendering);
        EXPECT_TRUE(object.mPhysics);
        EXPECT_FALSE(ray().mHit);
        EXPECT_FALSE(ptr.getRefData().getBaseNode());
    }

    TEST_F(PreparedCellTest, ManagerRejectsVfsModelAndPagingChanges)
    {
        storedReference();
        auto queue = std::make_shared<SceneUtil::WorkQueue>(0);
        osg::ref_ptr<PreparedTestContext> context = new PreparedTestContext;
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> compile = new osgUtil::IncrementalCompileOperation;
        compile->getContextSet().insert(context);
        auto* scene = mResources.getSceneManager();
        scene->setIncrementalCompileOperation(compile);
        MWWorld::CellPreloader loader(&mResources, &mShapes, nullptr, nullptr);
        loader.setWorkQueue(queue);
        loader.setMaxCacheSize(8);
        const auto prepareAndAcknowledge = [&] {
            loader.prepareCell(*mCell, 0);
            drain(*queue);
            acknowledge(*compile);
        };
        prepareAndAcknowledge();
        mVfs.buildIndex();
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 0));
        prepareAndAcknowledge();
        const auto modelEpoch = scene->modelSettingsRevision();
        scene->setAutoUseNormalMaps(true);
        ASSERT_GT(scene->modelSettingsRevision(), modelEpoch);
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 0));
        prepareAndAcknowledge();
        auto defines = scene->getShaderManager().getGlobalDefines();
        const auto shaderEpoch = scene->getShaderManager().compileRevision();
        defines["clamp"] = "0";
        scene->getShaderManager().setGlobalDefines(defines);
        ASSERT_GT(scene->getShaderManager().compileRevision(), shaderEpoch);
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 0));
        prepareAndAcknowledge();
        Settings::terrain().mDistantTerrain.set(true);
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 0));
    }

    TEST_F(PreparedCellTest, InstancePreloadingSettingSelectsCollisionOnlyAndInvalidatesOldMode)
    {
        storedReference();
        auto queue = std::make_shared<SceneUtil::WorkQueue>(0);
        MWWorld::CellPreloader loader(&mResources, &mShapes, nullptr, nullptr);
        loader.setWorkQueue(queue);
        loader.setMaxCacheSize(8);
        loader.setPreloadInstances(false);
        loader.prepareCell(*mCell, 0);
        drain(*queue);
        auto packet = loader.takePreparedCell(*mCell, 0);
        ASSERT_EQ(static_cast<bool>(packet), Misc::usePreparedCells());
        if (packet)
        {
            EXPECT_EQ(packet->compileRoot()->getNumChildren(), 0u);
        }
        loader.prepareCell(*mCell, 1);
        drain(*queue);
        loader.setPreloadInstances(true);
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 1));
    }

    TEST_F(PreparedCellTest, ManagerRejectsReleasedGraphicsAndOldSaveGeneration)
    {
        const auto ptr = storedReference();
        auto queue = std::make_shared<SceneUtil::WorkQueue>(0);
        osg::ref_ptr<PreparedTestContext> context = new PreparedTestContext;
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> compile = new osgUtil::IncrementalCompileOperation;
        compile->getContextSet().insert(context);
        mResources.getSceneManager()->setIncrementalCompileOperation(compile);
        MWWorld::CellPreloader loader(&mResources, &mShapes, nullptr, nullptr);
        loader.setWorkQueue(queue);
        loader.setMaxCacheSize(8);
        loader.prepareCell(*mCell, 0);
        drain(*queue);
        acknowledge(*compile);
        const auto epoch = mResources.getSceneManager()->graphicsRevision();
        mResources.getSceneManager()->releaseGLObjects(nullptr);
        EXPECT_GT(mResources.getSceneManager()->graphicsRevision(), epoch);
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 0));
        loader.prepareCell(*mCell, 1);
        drain(*queue);
        acknowledge(*compile);
        loader.clear();
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 1));
        EXPECT_FALSE(ptr.getRefData().getBaseNode());
        EXPECT_FALSE(ray().mHit);
    }

    TEST_F(PreparedCellTest, DedicatedPreparationQueueDoesNotOccupyTheRequiredArrivalQueue)
    {
        storedReference();
        Settings::terrain().mDistantTerrain.set(true);
        auto required = std::make_shared<SceneUtil::WorkQueue>(0);
        auto preparation = std::make_shared<SceneUtil::WorkQueue>(0);
        MWWorld::CellPreloader loader(&mResources, &mShapes, nullptr, nullptr);
        loader.setWorkQueue(required);
        loader.setPreparedWorkQueue(preparation);
        loader.setMaxCacheSize(8);
        loader.prepareCell(*mCell, 0);
        EXPECT_EQ(required->getNumItems(), 0u);
        EXPECT_EQ(preparation->getNumItems(), Misc::usePreparedCells() ? 1u : 0u);
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 0));
        drain(*preparation);
        EXPECT_EQ(static_cast<bool>(loader.takePreparedCell(*mCell, 0)), Misc::usePreparedCells());
        loader.prepareCell(*mCell, 1);
        loader.setPreparedWorkQueue(std::make_shared<SceneUtil::WorkQueue>(0));
        EXPECT_EQ(preparation->getNumItems(), 0u);
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 1));
    }

    TEST_F(PreparedCellTest, DisabledPreloadingAdmitsNoSpeculation)
    {
        storedReference();
        Settings::terrain().mDistantTerrain.set(true);
        Settings::cells().mPreloadEnabled.set(false);
        auto queue = std::make_shared<SceneUtil::WorkQueue>(0);
        MWWorld::CellPreloader loader(&mResources, &mShapes, nullptr, nullptr);
        loader.setWorkQueue(queue);
        loader.setMaxCacheSize(8);
        loader.prepareCell(*mCell, 0);
        EXPECT_EQ(queue->getNumItems(), 0u);
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 0));
    }

    TEST_F(PreparedCellTest, ManagerCancelsOldQueueAndPinsContextsDuringQueuedWork)
    {
        storedReference();
        auto queue = std::make_shared<SceneUtil::WorkQueue>(0);
        osg::ref_ptr<PreparedTestContext> context = new PreparedTestContext;
        osg::observer_ptr<osg::GraphicsContext> observed(context.get());
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> compile = new osgUtil::IncrementalCompileOperation;
        compile->getContextSet().insert(context);
        mResources.getSceneManager()->setIncrementalCompileOperation(compile);
        MWWorld::CellPreloader loader(&mResources, &mShapes, nullptr, nullptr);
        loader.setWorkQueue(queue);
        loader.setMaxCacheSize(8);
        loader.prepareCell(*mCell, 0);
        compile->getContextSet().clear();
        context = nullptr;
        EXPECT_EQ(observed.valid(), Misc::usePreparedCells());
        loader.setWorkQueue(std::make_shared<SceneUtil::WorkQueue>(0));
        EXPECT_EQ(queue->getNumItems(), 0u);
        EXPECT_FALSE(observed.valid());
        EXPECT_FALSE(loader.takePreparedCell(*mCell, 0));
    }

#ifdef OPENMW_TEST_PREPARED_CELLS_GL
    TEST_F(PreparedCellTest, ActualGraphicsCompilationPublishesVisibleGeometryOnlyAfterCompletion)
    {
        const auto ptr = reference();
        const auto packet = prepare({ ptr });
        osg::ref_ptr<PreparedEglContext> context = new PreparedEglContext;
        osg::ref_ptr<SceneUtil::GameplayCompileOperation> compile
            = new SceneUtil::GameplayCompileOperation(nullptr, 120);
        compile->addGraphicsContext(context);
        auto completion = SceneUtil::prepareCompileReadiness(packet->compileRoot(), { context });
        packet->setCompileCompletion(completion.mReady);
        compile->add(completion.mSet, false);
        ASSERT_FALSE(packet->ready());
        EXPECT_FALSE(ray().mHit);
        EXPECT_EQ(mRoot->getNumChildren(), 0u);
        // Actual context-bound operations and their actual completion callback, no fabricated acknowledgement.
        unsigned frames = 0;
        for (; frames < 128 && !packet->ready(); ++frames)
            (*compile)(context);
        ASSERT_TRUE(packet->ready());
        EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
        packet->detachCompileRoot();
        osg::ref_ptr<osgUtil::SceneView> view = new osgUtil::SceneView;
        view->setDefaults();
        view->setState(context->getState());
        view->setSceneData(mRoot);
        view->setViewport(0, 0, 32, 32);
        view->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
        view->setCullingMode(osg::CullSettings::NO_CULLING);
        view->setClearColor({ 0, 0, 0, 1 });
        view->setProjectionMatrixAsOrtho(-10, 10, -10, 10, 1, 100);
        view->setViewMatrixAsLookAt({ 10, 20, 60 }, { 10, 20, 30 }, { 0, 1, 0 });
        mRoot->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
        // RenderingManager supplies these common root values in the game.
        auto* rootState = mRoot->getOrCreateStateSet();
        rootState->addUniform(new osg::Uniform("alpha", 1.f));
        rootState->addUniform(new osg::Uniform("actorFade", 1.f));
        rootState->addUniform(new osg::Uniform("near", 1.f));
        rootState->addUniform(new osg::Uniform("far", 100.f));
        rootState->addUniform(new osg::Uniform("projectionMatrix", osg::Matrixf(view->getProjectionMatrix())));
        rootState->addUniform(new osg::Uniform("screenRes", osg::Vec2f(32, 32)));
        osg::ref_ptr<osg::Fog> fog = new osg::Fog;
        fog->setMode(osg::Fog::LINEAR); fog->setStart(100); fog->setEnd(200);
        rootState->setAttribute(fog);
        const auto draw = [&] {
            view->update(); view->cull(); view->draw();
            osgUtil::Statistics statistics;
            view->getStats(statistics);
            std::cout << "Prepared pixel fixture: drawables=" << statistics.numDrawables
                << " rootChildren=" << mRoot->getNumChildren() << '\n';
            std::array<unsigned char, 4> pixel{};
            glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
            return pixel;
        };
        const auto dormant = draw();
        EXPECT_EQ(dormant[0], 0); EXPECT_EQ(dormant[1], 0); EXPECT_EQ(dormant[2], 0);
        publish(ptr, packet->take(ptr, mModel));
        const auto visible = draw();
        EXPECT_GT(static_cast<int>(visible[0]) + visible[1] + visible[2], 0);
        EXPECT_EQ(ray().mHitObject, ptr);
        EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
        RecordProperty("actual_ico_frames", frames);
        RecordProperty("renderer", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        mRoot->releaseGLObjects(context->getState());
        mResources.getSceneManager()->releaseGLObjects(context->getState());
        compile->removeGraphicsContext(context);
    }
#endif

    TEST_F(PreparedCellTest, CacheMaintenancePreservesExplicitCompileCompletion)
    {
        osg::ref_ptr<PreparedTestContext> context = new PreparedTestContext;
        osg::ref_ptr<osgUtil::IncrementalCompileOperation> compile = new osgUtil::IncrementalCompileOperation;
        mResources.getSceneManager()->setIncrementalCompileOperation(compile);
        osg::ref_ptr<osg::Group> root = new osg::Group;
        auto completion = SceneUtil::prepareCompileReadiness(root, { context });
        compile->add(completion.mSet, false);
        mResources.getSceneManager()->updateCache(10);
        EXPECT_EQ(compile->getToCompile().size(), 1u);
        EXPECT_FALSE(completion.mReady->load());
        acknowledge(*compile);
        EXPECT_TRUE(completion.mReady->load());
        EXPECT_TRUE(compile->getToCompile().empty());
    }
}
