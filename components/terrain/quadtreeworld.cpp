#include <components/diagnostics/loading.hpp>
#include "quadtreeworld.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>
#include <numeric>
#include <typeinfo>
#include <unordered_set>
#include <osg/Texture>
#include <components/vfs/manager.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/compilepriority.hpp>
#include <components/misc/combinedperformance.hpp>
#include <components/debug/debuglog.hpp>

#include <components/diagnostics/timing.hpp>

#include <osg/PolygonMode>
#include <osg/ShapeDrawable>
#include <osgUtil/CullVisitor>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>

#include <limits>
#include <optional>

#include <components/esm/util.hpp>
#include <components/loadinglistener/reporter.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/mathutil.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/material.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "chunkmanager.hpp"
#include "compositemaprenderer.hpp"
#include "heightcull.hpp"
#include "quadtreenode.hpp"
#include "rayfootprint.hpp"
#include "storage.hpp"
#include "terraindrawable.hpp"
#include "viewdata.hpp"

namespace
{
    std::optional<Terrain::RayFootprint> getRayFootprint(osg::NodeVisitor& nv)
    {
        auto* visitor = dynamic_cast<osgUtil::IntersectionVisitor*>(&nv);
        if (!visitor || visitor->getNodeMaskOverride() || visitor->getModelMatrix())
            return {};
        auto* ray = dynamic_cast<osgUtil::LineSegmentIntersector*>(visitor->getIntersector());
        if (!ray || typeid(*ray) != typeid(osgUtil::LineSegmentIntersector)
            || ray->getCoordinateFrame() != osgUtil::Intersector::MODEL)
            return {};
        Terrain::RayFootprint result{ ray->getStart(), ray->getEnd() };
        if (!result.valid())
            return {};
        return result;
    }

    unsigned int Log2(unsigned int n)
    {
        unsigned int targetlevel = 0;
        while (n >>= 1)
            ++targetlevel;
        return targetlevel;
    }

}

namespace Terrain
{

    class DefaultLodCallback : public LodCallback
    {
    public:
        DefaultLodCallback(float factor, float minSize, float viewDistance, const osg::Vec4i& grid, int cellSizeInUnits,
            float distanceModifier = 0.f)
            : mFactor(factor)
            , mMinSize(minSize)
            , mViewDistance(viewDistance)
            , mActiveGrid(grid)
            , mDistanceModifier(distanceModifier)
            , mCellSizeInUnits(cellSizeInUnits)
        {
        }

        ReturnValue isSufficientDetail(QuadTreeNode* node, float dist) override
        {
            const osg::Vec2f& center = node->getCenter();
            bool activeGrid = (center.x() > mActiveGrid.x() && center.y() > mActiveGrid.y()
                && center.x() < mActiveGrid.z() && center.y() < mActiveGrid.w());

            if (node->getSize() > 1)
            {
                float halfSize = node->getSize() / 2;
                osg::Vec4i nodeBounds(static_cast<int>(center.x() - halfSize), static_cast<int>(center.y() - halfSize),
                    static_cast<int>(center.x() + halfSize), static_cast<int>(center.y() + halfSize));
                bool intersects = (std::max(nodeBounds.x(), mActiveGrid.x()) < std::min(nodeBounds.z(), mActiveGrid.z())
                    && std::max(nodeBounds.y(), mActiveGrid.y()) < std::min(nodeBounds.w(), mActiveGrid.w()));
                // to prevent making chunks who will cross the activegrid border
                if (intersects)
                    return Deeper;
            }
            dist = std::max(0.f, dist + mDistanceModifier);
            if (dist > mViewDistance && !activeGrid) // for Scene<->ObjectPaging sync the activegrid must remain loaded
                return StopTraversal;
            return getNativeLodLevel(node, mMinSize)
                    <= convertDistanceToLodLevel(dist, mMinSize, mFactor, mCellSizeInUnits)
                ? StopTraversalAndUse
                : Deeper;
        }
        static unsigned int getNativeLodLevel(const QuadTreeNode* node, float minSize)
        {
            return Log2(static_cast<unsigned int>(node->getSize() / minSize));
        }
        static unsigned int convertDistanceToLodLevel(float dist, float minSize, float factor, int cellSize)
        {
            return Log2(static_cast<unsigned int>(dist / (cellSize * minSize * factor)));
        }

    private:
        float mFactor;
        float mMinSize;
        float mViewDistance;
        osg::Vec4i mActiveGrid;
        float mDistanceModifier;
        int mCellSizeInUnits;
    };

    class RootNode : public QuadTreeNode
    {
    public:
        RootNode(float size, const osg::Vec2f& center)
            : QuadTreeNode(nullptr, Root, size, center)
            , mWorld(nullptr)
        {
        }

        void setWorld(QuadTreeWorld* world) { mWorld = world; }

        void accept(osg::NodeVisitor& nv) override
        {
            if (!nv.validNodeMask(*this))
                return;
            nv.pushOntoNodePath(this);
            mWorld->accept(nv);
            nv.popFromNodePath();
        }

    private:
        QuadTreeWorld* mWorld;
    };

    class QuadTreeBuilder
    {
    public:
        QuadTreeBuilder(Terrain::Storage* storage, float minSize, ESM::RefId worldspace)
            : mStorage(storage)
            , mMinX(0.f)
            , mMaxX(0.f)
            , mMinY(0.f)
            , mMaxY(0.f)
            , mMinSize(minSize)
            , mWorldspace(worldspace)
        {
        }

        void build()
        {
            mStorage->getBounds(mMinX, mMaxX, mMinY, mMaxY, mWorldspace);

            int origSizeX = static_cast<int>(mMaxX - mMinX);
            int origSizeY = static_cast<int>(mMaxY - mMinY);

            // Dividing a quad tree only works well for powers of two, so round up to the nearest one
            int size = Misc::nextPowerOfTwo(std::max(origSizeX, origSizeY));

            float centerX = (mMinX + mMaxX) / 2.f + (size - origSizeX) / 2.f;
            float centerY = (mMinY + mMaxY) / 2.f + (size - origSizeY) / 2.f;

            mRootNode = new RootNode(static_cast<float>(size), osg::Vec2f(centerX, centerY));
            addChildren(mRootNode);

            mRootNode->initNeighbours();
            float cellWorldSize = mStorage->getCellWorldSize(mWorldspace);
            mRootNode->setInitialBound(
                osg::BoundingSphere(osg::BoundingBox(osg::Vec3(mMinX * cellWorldSize, mMinY * cellWorldSize, 0),
                    osg::Vec3(mMaxX * cellWorldSize, mMaxY * cellWorldSize, 0))));
        }

        void addChildren(QuadTreeNode* parent)
        {
            float halfSize = parent->getSize() / 2.f;
            osg::BoundingBox boundingBox;
            for (unsigned int i = 0; i < 4; ++i)
            {
                osg::ref_ptr<QuadTreeNode> child = addChild(parent, static_cast<ChildDirection>(i), halfSize);
                if (child)
                {
                    boundingBox.expandBy(child->getBoundingBox());
                    parent->addChildNode(child);
                }
            }

            if (!boundingBox.valid())
                parent->removeChildren(0, 4);
            else
                parent->setBoundingBox(boundingBox);
        }

        osg::ref_ptr<QuadTreeNode> addChild(QuadTreeNode* parent, ChildDirection direction, float size)
        {
            float halfSize = size / 2.f;
            osg::Vec2f center;
            switch (direction)
            {
                case SW:
                    center = parent->getCenter() + osg::Vec2f(-halfSize, -halfSize);
                    break;
                case SE:
                    center = parent->getCenter() + osg::Vec2f(halfSize, -halfSize);
                    break;
                case NW:
                    center = parent->getCenter() + osg::Vec2f(-halfSize, halfSize);
                    break;
                case NE:
                    center = parent->getCenter() + osg::Vec2f(halfSize, halfSize);
                    break;
                default:
                    break;
            }

            osg::ref_ptr<QuadTreeNode> node = new QuadTreeNode(parent, direction, size, center);

            if (center.x() - halfSize > mMaxX || center.x() + halfSize < mMinX || center.y() - halfSize > mMaxY
                || center.y() + halfSize < mMinY)
            // Out of bounds of the actual terrain - this will happen because
            // we rounded the size up to the next power of two
            {
                // Still create and return an empty node so as to not break the assumption that each QuadTreeNode has
                // either 4 or 0 children.
                return node;
            }

            // Do not add child nodes for default cells without data.
            // size = 1 means that the single shape covers the whole cell.
            if (node->getSize() == 1
                && !mStorage->hasData(ESM::ExteriorCellLocation(
                    static_cast<int>(center.x() - 0.5f), static_cast<int>(center.y() - 0.5f), mWorldspace)))
                return node;

            if (node->getSize() <= mMinSize)
            {
                // We arrived at a leaf.
                // Since the tree is used for LOD level selection instead of culling, we do not need to load the actual
                // height data here.
                constexpr float minZ = -std::numeric_limits<float>::max();
                constexpr float maxZ = std::numeric_limits<float>::max();
                float cellWorldSize = mStorage->getCellWorldSize(mWorldspace);
                osg::BoundingBox boundingBox(
                    osg::Vec3f((center.x() - halfSize) * cellWorldSize, (center.y() - halfSize) * cellWorldSize, minZ),
                    osg::Vec3f((center.x() + halfSize) * cellWorldSize, (center.y() + halfSize) * cellWorldSize, maxZ));
                node->setBoundingBox(boundingBox);
                return node;
            }
            else
            {
                addChildren(node);
                return node;
            }
        }

        osg::ref_ptr<RootNode> getRootNode() { return mRootNode; }

    private:
        Terrain::Storage* mStorage;

        float mMinX, mMaxX, mMinY, mMaxY;
        float mMinSize;

        osg::ref_ptr<RootNode> mRootNode;
        ESM::RefId mWorldspace;
    };

    class DebugChunkManager : public QuadTreeWorld::ChunkManager
    {
    public:
        DebugChunkManager(
            Resource::SceneManager* sceneManager, Storage* storage, unsigned int nodeMask, ESM::RefId worldspace)
            : QuadTreeWorld::ChunkManager(worldspace)
            , mSceneManager(sceneManager)
            , mStorage(storage)
            , mNodeMask(nodeMask)
        {
        }
        osg::ref_ptr<osg::Node> getChunk(float size, const osg::Vec2f& chunkCenter, unsigned char /*lod*/,
            unsigned int lodFlags, bool activeGrid, const osg::Vec3f& viewPoint, bool compile, const std::atomic_bool*)
        {
            osg::Vec3f center = { chunkCenter.x(), chunkCenter.y(), 0 };
            auto chunkBorder = CellBorder::createBorderGeometry(center.x() - size / 2.f, center.y() - size / 2.f, size,
                mStorage, mSceneManager, mNodeMask, mWorldspace, 5.f, { 1, 0, 0, 0 });
            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> pat = new SceneUtil::PositionAttitudeTransform;
            pat->setPosition(-center * static_cast<float>(ESM::getCellSize(mWorldspace)));
            pat->addChild(chunkBorder);
            return pat;
        }
        unsigned int getNodeMask() { return mNodeMask; }

    private:
        Resource::SceneManager* mSceneManager;
        Storage* mStorage;
        unsigned int mNodeMask;
    };

    QuadTreeWorld::QuadTreeWorld(osg::Group* parent, osg::Group* compileRoot, Resource::ResourceSystem* resourceSystem,
        Storage* storage, unsigned int nodeMask, unsigned int preCompileMask, unsigned int borderMask,
        int compMapResolution, float compMapLevel, float lodFactor, int vertexLodMod, float maxCompGeometrySize,
        bool debugChunks, ESM::RefId worldspace, double expiryDelay)
        : TerrainGrid(
            parent, compileRoot, resourceSystem, storage, nodeMask, worldspace, expiryDelay, preCompileMask, borderMask)
        , mViewDataMap(std::make_unique<ViewDataMap>())
        , mQuadTreeBuilt(false)
        , mLodFactor(lodFactor)
        , mVertexLodMod(vertexLodMod)
        , mViewDistance(std::numeric_limits<float>::max())
        , mMinSize(ESM::isEsm4Ext(worldspace) ? 1 / 2.f : 1 / 8.f)
        , mDebugTerrainChunks(debugChunks)
    {
        mChunkManager->setCompositeMapSize(compMapResolution);
        mChunkManager->setCompositeMapLevel(
            ESM::isEsm4Ext(worldspace) ? compMapLevel * 2 /*because cells are twice smaller*/ : compMapLevel);
        mChunkManager->setMaxCompositeGeometrySize(maxCompGeometrySize);
        mChunkManagers.push_back(mChunkManager.get());

        if (mDebugTerrainChunks)
        {
            mDebugChunkManager = std::make_unique<DebugChunkManager>(
                mResourceSystem->getSceneManager(), mStorage, borderMask, mWorldspace);
            addChunkManager(mDebugChunkManager.get());
        }
    }

    QuadTreeWorld::~QuadTreeWorld() {}

    /// get the level of vertex detail to render this node at, expressed relative to the native resolution of the vertex
    /// data set, NOT relative to mMinSize as is the case with node LODs.
    unsigned int getVertexLod(QuadTreeNode* node, int vertexLodMod)
    {
        unsigned int vertexLod = DefaultLodCallback::getNativeLodLevel(node, 1);
        if (vertexLodMod > 0)
        {
            vertexLod = static_cast<unsigned int>(std::max(0, static_cast<int>(vertexLod) - vertexLodMod));
        }
        else if (vertexLodMod < 0)
        {
            float size = node->getSize();
            // Stop to simplify at this level since with size = 1 the node already covers the whole cell and has
            // getCellVertices() vertices.
            while (size < 1)
            {
                size *= 2;
                vertexLodMod = std::min(0, vertexLodMod + 1);
            }
            vertexLod += std::abs(vertexLodMod);
        }
        return vertexLod;
    }

    /// get the flags to use for stitching in the index buffer so that chunks of different LOD connect seamlessly
    unsigned int getLodFlags(QuadTreeNode* node, unsigned int ourVertexLod, int vertexLodMod, const ViewData* vd)
    {
        unsigned int lodFlags = 0;
        for (unsigned int i = 0; i < 4; ++i)
        {
            QuadTreeNode* neighbour = node->getNeighbour(static_cast<Direction>(i));

            // If the neighbour isn't currently rendering itself,
            // go up until we find one. NOTE: We don't need to go down,
            // because in that case neighbour's detail would be higher than
            // our detail and the neighbour would handle stitching by itself.
            while (neighbour && !vd->contains(neighbour))
                neighbour = neighbour->getParent();
            unsigned int lod = 0;
            if (neighbour)
                lod = getVertexLod(neighbour, vertexLodMod);

            if (lod <= ourVertexLod) // We only need to worry about neighbours less detailed than we are -
                lod = 0; // neighbours with more detail will do the stitching themselves
            // Use 4 bits for each LOD delta
            if (lod > 0)
            {
                lodFlags |= (lod - ourVertexLod) << (4 * i);
            }
        }
        // Use the remaining bits for our vertex LOD
        lodFlags |= (ourVertexLod << (4 * 4));
        return lodFlags;
    }

    void QuadTreeWorld::updateRenderingLod(ViewDataEntry& entry, ViewData* vd)
    {
        if (vd->hasChanged())
        {
            vd->buildNodeIndex();

            unsigned int ourVertexLod = getVertexLod(entry.mNode, mVertexLodMod);
            // have to recompute the lodFlags in case a neighbour has changed LOD.
            unsigned int lodFlags = getLodFlags(entry.mNode, ourVertexLod, mVertexLodMod, vd);
            if (lodFlags != entry.mLodFlags)
            {
                entry.mRenderingNode = nullptr;
                entry.mLodFlags = lodFlags;
            }
        }
    }

    QuadTreeWorld::ChunkRequest QuadTreeWorld::getChunkRequest(const ViewDataEntry& entry, const osg::Vec4i& bounds) const
    {
        const auto& center = entry.mNode->getCenter();
        const bool active = center.x() > bounds.x() && center.y() > bounds.y()
            && center.x() < bounds.z() && center.y() < bounds.w();
        return { entry.mNode->getSize(), center, entry.mLodFlags, active };
    }

    void QuadTreeWorld::loadRenderingNode(ViewDataEntry& entry, ViewData* vd, float cellWorldSize,
        const ChunkRequest& request, bool compile, const std::atomic_bool* abort, const BatchChunkManager::Fetch* fetch,
        bool asynchronous, double referenceTime, bool separateScenery)
    {
        if (entry.needsRendering(asynchronous && !request.mActiveGrid, separateScenery && !request.mActiveGrid))
        {
            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> pat = new SceneUtil::PositionAttitudeTransform;
            pat->setPosition(osg::Vec3f(
                entry.mNode->getCenter().x() * cellWorldSize, entry.mNode->getCenter().y() * cellWorldSize, 0.f));

            for (QuadTreeWorld::ChunkManager* m : mChunkManagers)
            {
                if (abort && abort->load(std::memory_order_relaxed))
                    return;
                if (separateScenery && !request.mActiveGrid && m->usesSceneryCoverage())
                    continue;
                osg::ref_ptr<osg::Node> n;
                const auto result = asynchronous && !request.mActiveGrid
                    ? m->requestChunk(request, vd->getViewPoint(), referenceTime, false) : ChunkManager::AsyncResult{};
                if (result.mSupported && result.mReady)
                {
                    Diagnostics::Loading::Timer prepared(Diagnostics::Loading::Stage::SceneryPrepared);
                    n = result.mNode;
                }
                else if (fetch && m == mBatchChunkManager)
                    n = (*fetch)();
                else
                {
                    std::optional<Diagnostics::Loading::Timer> required;
                    if (asynchronous && !request.mActiveGrid && m == mBatchChunkManager)
                        required.emplace(Diagnostics::Loading::Stage::SceneryRequired);
                    n = m->getChunk(request.mSize, request.mCenter,
                        static_cast<unsigned char>(DefaultLodCallback::getNativeLodLevel(entry.mNode, mMinSize)),
                        request.mLodFlags, request.mActiveGrid, vd->getViewPoint(), compile, abort);
                }
                if (n)
                    pat->addChild(n);
            }
            if (!abort || !abort->load(std::memory_order_relaxed))
            {
                entry.mRenderingNode = pat;
                entry.mRenderingComplete = true;
                entry.mRenderingReadyForCamera = asynchronous || request.mActiveGrid;
                entry.mRenderingSeparateScenery = separateScenery && !request.mActiveGrid;
            }
        }
    }

    void QuadTreeWorld::prepareAhead(ViewData& current, const osg::Vec3f& point, double referenceTime, float cellWorldSize)
    {
        if (!mBatchChunkManager || !mBatchChunkManager->canPrepareAsync())
            return;
        const auto plan = current.mStreamingLookAhead.update(point, mActiveGrid, referenceTime, cellWorldSize);
        if (!plan)
            return;
        Diagnostics::Loading::Timer timing(Diagnostics::Loading::Stage::SceneryAhead);
        // Distant object identity excludes terrain stitching flags. Keep flags
        // in the request, but do not queue an already-visible object chunk
        // merely because a neighboring terrain tile changed its stitch.
        using Key = std::tuple<osg::Vec2f, float>;
        std::set<Key> visible;
        for (unsigned i = 0; i < current.getNumEntries(); ++i)
        {
            const auto request = getChunkRequest(current.getEntry(i), mActiveGrid);
            if (!request.mActiveGrid)
                visible.emplace(request.mCenter, request.mSize);
        }
        struct Candidate
        {
            ChunkRequest mRequest;
            osg::Vec3f mPoint;
            float mDistance;
        };
        std::map<Key, Candidate> candidates;
        const auto select = [&](const osg::Vec3f& target, const osg::Vec4i& grid) {
            ViewData ahead;
            ahead.setViewPoint(target);
            ahead.setActiveGrid(grid);
            const float distance = std::min(mViewDistance.load(), std::numeric_limits<float>::max() - plan->mMargin)
                + plan->mMargin;
            DefaultLodCallback lod(mLodFactor, mMinSize, distance, grid, static_cast<int>(cellWorldSize));
            mRootNode->traverseNodes(&ahead, target, &lod);
            for (unsigned i = 0; i < ahead.getNumEntries(); ++i)
            {
                auto& entry = ahead.getEntry(i);
                updateRenderingLod(entry, &ahead);
                const auto request = getChunkRequest(entry, grid);
                const Key key{ request.mCenter, request.mSize };
                if (request.mActiveGrid || visible.contains(key))
                    continue;
                const auto delta = request.mCenter * cellWorldSize
                    - osg::Vec2f(plan->mPredicted.x(), plan->mPredicted.y());
                candidates.try_emplace(key, Candidate{ request, target, delta.length2() });
            }
        };
        // Forecast actual future LOD identities as well as the outer ring. A
        // larger radius alone would miss the parent/child changes during travel.
        select(plan->mPredicted, plan->mPredictedGrid);
        if ((plan->mPredicted - plan->mCurrent).length2() > 1.f)
            select(plan->mCurrent, mActiveGrid);
        std::vector<Candidate> ordered;
        ordered.reserve(candidates.size());
        for (const auto& [key, candidate] : candidates)
            ordered.push_back(candidate);
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            return left.mDistance < right.mDistance;
        });
        const auto count = std::min<std::size_t>(ordered.size(), 256);
        for (std::size_t i = 0; i < count; ++i)
            mBatchChunkManager->prepareScenery(ordered[i].mRequest, ordered[i].mPoint, referenceTime);
    }

    namespace
    {
        osg::ref_ptr<osg::StateSet> makeStateSet()
        {
            osg::ref_ptr<osg::StateSet> stateSet = new osg::StateSet;
            stateSet->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
            stateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
            stateSet->setAttributeAndModes(
                new osg::PolygonMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::LINE),
                osg::StateAttribute::ON);
            osg::ref_ptr<SceneUtil::Material> material = new SceneUtil::Material;
            material->setEmission(osg::Vec4f(0, 0, 1, 1));
            material->setDiffuse(osg::Vec4f(0, 0, 0, 1));
            material->setAmbient(osg::Vec4f(0, 0, 0, 1));
            material->updateStateSet(stateSet);
            stateSet->setAttributeAndModes(material, osg::StateAttribute::ON);
            stateSet->setRenderBinDetails(100, "RenderBin");
            return stateSet;
        }

        void updateWaterCullingView(
            HeightCullCallback* callback, ViewData* vd, osgUtil::CullVisitor* cv, float cellworldsize, bool outofworld)
        {
            if (!(cv->getTraversalMask() & callback->getCullMask()))
                return;
            float lowZ = std::numeric_limits<float>::max();
            float highZ = callback->getHighZ();
            if (cv->getEyePoint().z() <= highZ || outofworld)
            {
                callback->setLowZ(-std::numeric_limits<float>::max());
                return;
            }
            cv->pushCurrentMask();
            static bool debug = getenv("OPENMW_WATER_CULLING_DEBUG") != nullptr;
            for (unsigned int i = 0; i < vd->getNumEntries(); ++i)
            {
                ViewDataEntry& entry = vd->getEntry(i);
                if (!entry.mRenderingNode || !entry.mRenderingNode->asGroup()
                    || !entry.mRenderingNode->asGroup()->getNumChildren())
                {
                    // A far water tile may still be streaming. Keep the water surface visible.
                    lowZ = -std::numeric_limits<float>::max();
                    break;
                }
                const auto* terrain = dynamic_cast<const TerrainDrawable*>(entry.mRenderingNode->asGroup()->getChild(0));
                if (!terrain)
                {
                    lowZ = -std::numeric_limits<float>::max();
                    break;
                }
                osg::BoundingBox bb = terrain->getWaterBoundingBox();
                if (!bb.valid())
                    continue;
                osg::Vec3f ofs(
                    entry.mNode->getCenter().x() * cellworldsize, entry.mNode->getCenter().y() * cellworldsize, 0.f);
                bb._min += ofs;
                bb._max += ofs;
                bb._min.z() = highZ;
                bb._max.z() = highZ;
                if (cv->isCulled(bb))
                    continue;
                lowZ = bb._min.z();

                if (!debug)
                    break;
                osg::Box* b = new osg::Box;
                b->set(bb.center(), bb._max - bb.center());
                osg::ShapeDrawable* drw = new osg::ShapeDrawable(b);
                static const osg::ref_ptr<osg::StateSet> stateset = makeStateSet();
                drw->setStateSet(stateset);
                drw->accept(*cv);
            }
            callback->setLowZ(lowZ);
            cv->popCurrentMask();
        }
    }

    void QuadTreeWorld::accept(osg::NodeVisitor& nv)
    {
        bool isCullVisitor = nv.getVisitorType() == osg::NodeVisitor::CULL_VISITOR;
        if (!isCullVisitor && nv.getVisitorType() != osg::NodeVisitor::INTERSECTION_VISITOR)
            return;

        const double referenceTime = nv.getFrameStamp() ? nv.getFrameStamp()->getReferenceTime() : 0.0;
        if (mBatchChunkManager && mBatchChunkManager->updateAsyncState(referenceTime))
            rebuildViews();

        Diagnostics::Loading::Metrics cullMetrics;
        std::optional<Diagnostics::Loading::Session> cullCapture;
        Diagnostics::Loading::Clock::time_point cullStart{};
        if (isCullVisitor)
        {
            cullCapture.emplace(cullMetrics);
            cullStart = Diagnostics::Loading::Clock::now();
        }

        std::optional<Diagnostics::Scope> acceptTiming;
        if (!isCullVisitor)
            acceptTiming.emplace("terrain", "ray", "accept");
        osg::Object* viewer = isCullVisitor ? static_cast<osgUtil::CullVisitor*>(&nv)->getCurrentCamera() : nullptr;
        bool needsUpdate = true;
        osg::Vec3f viewPoint = viewer ? nv.getViewPoint() : nv.getEyePoint();
        ViewData* vd = mViewDataMap->getViewData(viewer, viewPoint, mActiveGrid, needsUpdate);
        if (needsUpdate)
        {
            std::optional<Diagnostics::Scope> selectTiming;
            if (!isCullVisitor)
                selectTiming.emplace("terrain", "ray", "selectLod");
            Diagnostics::Loading::Timer selection(Diagnostics::Loading::Stage::ViewSelection);
            vd->reset();
            DefaultLodCallback lodCallback(
                mLodFactor, mMinSize, mViewDistance, mActiveGrid, ESM::getCellSize(mWorldspace));
            mRootNode->traverseNodes(vd, viewPoint, &lodCallback);
        }

        const float cellWorldSize = static_cast<float>(ESM::getCellSize(mWorldspace));
        const bool asynchronous = isCullVisitor && nv.getFrameStamp() && Misc::useAsyncObjectPaging();
        const bool separateScenery = isCullVisitor && mBatchChunkManager
            && mBatchChunkManager->usesSceneryCoverage();
        std::vector<ChunkRequest> scenery;

        const auto ray = isCullVisitor ? std::optional<RayFootprint>{} : getRayFootprint(nv);
        auto arrival = (isCullVisitor || ray) ? mVisibleArrival.load() : nullptr;
        if (arrival && (!validArrival(*arrival) || !arrival->matches(viewPoint, mActiveGrid, cellWorldSize, arrival->mRevision)))
            arrival.reset();
        if (arrival && !nv.getFrameStamp())
            arrival.reset(); // offscreen visitors without a frame clock keep normal synchronous semantics
        for (unsigned int i = 0; i < vd->getNumEntries(); ++i)
        {
            ViewDataEntry& entry = vd->getEntry(i);
            {
                Diagnostics::Loading::Timer prepare(Diagnostics::Loading::Stage::CullPrepare);
                std::optional<Diagnostics::Scope> prepareTiming;
                if (!isCullVisitor)
                    prepareTiming.emplace("terrain", "ray", "prepareRenderingNode");
                updateRenderingLod(entry, vd);
                const auto request = getChunkRequest(entry, mActiveGrid);
                if (separateScenery && !request.mActiveGrid)
                    scenery.push_back(request);
                if (entry.needsRendering(asynchronous && !request.mActiveGrid, separateScenery && !request.mActiveGrid))
                {
                    if (arrival && !entry.mRenderingNode && (isCullVisitor || request.mActiveGrid
                            || !mBatchChunkManager || !mBatchChunkManager->usesSceneryCoverage()))
                    {
                        const ArrivalView::Key key{ entry.mNode, entry.mLodFlags };
                        auto prepared = arrival->find(key);
                        // A CPU-ready preload uses normal draw compilation. It
                        // is not an async GL receipt, and missing required tiles
                        // are never withheld to satisfy an attachment quota.
                        entry.mRenderingNode = std::move(prepared);
                        entry.mRenderingComplete = true;
                        entry.mRenderingReadyForCamera = isCullVisitor;
                        entry.mRenderingSeparateScenery = separateScenery && !request.mActiveGrid;
                    }
                    if (entry.needsRendering() && ray
                        && !ray->intersects(request.mCenter.x() * cellWorldSize,
                            request.mCenter.y() * cellWorldSize, request.mSize * cellWorldSize * 0.5))
                    {
                        // Ground is bounded by the tile, but a paged mesh or an added
                        // manager may extend across it. Negative results are query-local.
                        const auto mayHit = [&](ChunkManager* manager) {
                            if (manager == mChunkManager.get())
                                return false;
                            const auto mask = manager->getNodeMask();
                            return (!mask || (mask & nv.getTraversalMask()))
                                && manager->mayIntersectOutsideTile(request, vd->getViewPoint(), *ray);
                        };
                        if (std::none_of(mChunkManagers.begin(), mChunkManagers.end(), mayHit))
                            continue;
                    }
                    if (entry.needsRendering(asynchronous && !request.mActiveGrid, separateScenery && !request.mActiveGrid))
                        loadRenderingNode(entry, vd, cellWorldSize, request, false, nullptr, nullptr,
                            asynchronous, referenceTime, separateScenery);
                }
            }
            {
                Diagnostics::Loading::Timer traverse(Diagnostics::Loading::Stage::CullTraverse);
                std::optional<Diagnostics::Scope> traverseTiming;
                if (!isCullVisitor)
                    traverseTiming.emplace("terrain", "ray", "traverseRenderingNode");
                entry.mRenderingNode->accept(nv);
            }
        }

        if (separateScenery)
        {
            auto incoming = arrival ? arrival : mPreloadedScenery.load();
            const bool standby = incoming && !arrival;
            if (standby && (std::chrono::steady_clock::now() - incoming->mCreated > std::chrono::seconds(30)
                    || !validArrival(*incoming)))
            {
                auto expired = incoming;
                mPreloadedScenery.compare_exchange_strong(expired, nullptr);
                incoming.reset();
            }
            if (incoming && (!validArrival(*incoming)
                    || !incoming->matches(viewPoint, mActiveGrid, cellWorldSize, incoming->mRevision)))
                incoming.reset();
            mBatchChunkManager->acceptScenery(scenery, viewPoint, mActiveGrid, nv, referenceTime, vd->mScenery,
                incoming ? incoming->scenery() : nullptr);
            // Once consumed, selected coverage belongs to the camera snapshot.
            // Do not keep a second entire standby view alive indefinitely.
            if (standby && incoming)
                mPreloadedScenery.compare_exchange_strong(incoming, nullptr);
        }

        if (asynchronous && mBatchChunkManager)
            prepareAhead(*vd, viewPoint, referenceTime, cellWorldSize);

        if (mHeightCullCallback && isCullVisitor)
            updateWaterCullingView(mHeightCullCallback, vd, static_cast<osgUtil::CullVisitor*>(&nv),
                mStorage->getCellWorldSize(mWorldspace), !isGridEmpty());

        vd->resetChanged();

        if (referenceTime != 0.0)
        {
            vd->setLastUsageTimeStamp(referenceTime);
            mViewDataMap->clearUnusedViews(referenceTime);
        }
        if (isCullVisitor)
        {
            if (asynchronous)
            {
                using Stage = Diagnostics::Loading::Stage;
                mSceneryPrepared += cullMetrics.calls[static_cast<std::size_t>(Stage::SceneryPrepared)];
                mSceneryRequired += cullMetrics.calls[static_cast<std::size_t>(Stage::SceneryRequired)];
                mSceneryAhead += cullMetrics.calls[static_cast<std::size_t>(Stage::SceneryAhead)];
                mSceneryCancelled += cullMetrics.calls[static_cast<std::size_t>(Stage::SceneryCancelled)];
                mSceneryRequiredMs += cullMetrics.elapsedMs[static_cast<std::size_t>(Stage::SceneryRequired)];
                mSceneryAheadMs += cullMetrics.elapsedMs[static_cast<std::size_t>(Stage::SceneryAhead)];
                if (referenceTime >= mNextSceneryReport && (mSceneryPrepared || mSceneryRequired || mSceneryAhead))
                {
                    Log(Debug::Info) << "Scenery preparation: preparedHits=" << mSceneryPrepared
                        << " requiredFallbacks=" << mSceneryRequired << " requiredMs=" << mSceneryRequiredMs
                        << " aheadPasses=" << mSceneryAhead << " aheadMs=" << mSceneryAheadMs
                        << " deadlineCancellations=" << mSceneryCancelled;
                    mSceneryPrepared = mSceneryRequired = mSceneryAhead = mSceneryCancelled = 0;
                    mSceneryRequiredMs = 0;
                    mSceneryAheadMs = 0;
                    mNextSceneryReport = referenceTime + 1.0;
                }
            }
            const double elapsed = Diagnostics::Loading::milliseconds(Diagnostics::Loading::Clock::now() - cullStart);
            if (elapsed >= 50)
                Log(Debug::Info) << "Terrain cull timing: totalMs=" << elapsed << " view=" << viewPoint.x() << ','
                                 << viewPoint.y() << ',' << viewPoint.z() << " entries=" << vd->getNumEntries()
                                 << cullMetrics;
        }
    }

    void QuadTreeWorld::ensureQuadTreeBuilt()
    {
        std::lock_guard<std::mutex> lock(mQuadTreeMutex);
        if (mQuadTreeBuilt)
            return;

        QuadTreeBuilder builder(mStorage, mMinSize, mWorldspace);
        builder.build();

        mRootNode = builder.getRootNode();
        mRootNode->setWorld(this);
        mQuadTreeBuilt = true;
    }

    void QuadTreeWorld::enable(bool enabled)
    {
        if (enabled)
        {
            ensureQuadTreeBuilt();

            if (!mRootNode->getNumParents())
                mTerrainRoot->addChild(mRootNode);
        }
        else if (mRootNode)
            mTerrainRoot->removeChild(mRootNode);
    }

    std::unique_ptr<View> QuadTreeWorld::createView()
    {
        return mViewDataMap->createIndependentView();
    }

    void QuadTreeWorld::preload(View* view, const osg::Vec3f& viewPoint, const osg::Vec4i& grid,
        std::atomic<bool>& abort, Loading::Reporter& reporter)
    {
        prepareView(view, viewPoint, grid, abort, reporter, {}, {});
    }

    std::shared_ptr<ArrivalView> QuadTreeWorld::createArrival(const osg::Vec3f& point, const osg::Vec4i& grid)
    {
        return std::make_shared<ArrivalView>(point, grid, mArrivalRevision.load(), mResourceSystem->getVFS()->getIndexRevision(),
            mResourceSystem->getSceneManager()->modelSettingsRevision());
    }

    bool QuadTreeWorld::validArrival(const ArrivalView& view) const
    {
        return view.mRevision == mArrivalRevision.load() && view.mVfsRevision == mResourceSystem->getVFS()->getIndexRevision()
            && view.mModelRevision == mResourceSystem->getSceneManager()->modelSettingsRevision();
    }

    void QuadTreeWorld::showArrival(std::shared_ptr<ArrivalView> view)
    {
        mVisibleArrival.store(std::move(view));
    }

    void QuadTreeWorld::preloadArrival(View* view, const std::shared_ptr<ArrivalView>& arrival,
        std::atomic_bool& abort, Loading::Reporter& reporter, const ArrivalView::Progress& nearReady)
    {
        prepareView(view, arrival->mPoint, arrival->mGrid, abort, reporter, arrival, nearReady);
    }

    namespace
    {
        // Account shared CPU geometry and a texture allowance once per view. The estimate is used
        // for retaining a recent exterior, never for dropping currently required scenery.
        class ArrivalFootprint : public osg::NodeVisitor
        {
            std::unordered_set<const osg::Object*> mSeen;
            std::size_t mBytes = 0;
            void buffer(const osg::BufferData* data)
            {
                if (data && mSeen.insert(data).second)
                    mBytes += 128 + data->getTotalDataSize();
            }
            void state(const osg::StateSet* set)
            {
                if (!set || !mSeen.insert(set).second)
                    return;
                mBytes += 512;
                for (unsigned unit = 0; unit < set->getTextureAttributeList().size(); ++unit)
                {
                    const auto* t = dynamic_cast<const osg::Texture*>(set->getTextureAttribute(unit, osg::StateAttribute::TEXTURE));
                    if (!t || !mSeen.insert(t).second)
                        continue;
                    // The graphics thread can release image slots during apply.
                    // Like compile readiness, use a conservative admission
                    // allowance without reading mutable texture image state.
                    mBytes += 256 + 4ull * 1024 * 1024;
                }
            }
        public:
            ArrivalFootprint() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) { setNodeMaskOverride(~0u); }
            void apply(osg::Node& node) override
            {
                if (!mSeen.insert(&node).second)
                    return;
                mBytes += 512;
                state(node.getStateSet());
                if (auto* geometry = node.asGeometry())
                {
                    buffer(geometry->getVertexArray());
                    buffer(geometry->getNormalArray());
                    buffer(geometry->getColorArray());
                    buffer(geometry->getSecondaryColorArray());
                    buffer(geometry->getFogCoordArray());
                    for (const auto& a : geometry->getTexCoordArrayList()) buffer(a.get());
                    for (const auto& a : geometry->getVertexAttribArrayList()) buffer(a.get());
                    for (unsigned i = 0; i < geometry->getNumPrimitiveSets(); ++i) buffer(geometry->getPrimitiveSet(i));
                    if (auto* land = dynamic_cast<TerrainDrawable*>(geometry))
                        for (const auto& pass : land->getPasses()) state(pass.get());
                }
                traverse(node);
            }
            std::size_t measure(osg::Node& node)
            {
                const auto before = mBytes;
                node.accept(*this);
                return mBytes - before;
            }
        };
    }

    std::shared_ptr<ArrivalView> QuadTreeWorld::captureArrival(const osg::Vec3f& point, const osg::Vec4i& grid,
        std::size_t budget)
    {
        auto* cached = mViewDataMap->getCompletedView(grid);
        if (!cached || cached->getNumEntries() > 512)
            return {};
        auto result = std::make_shared<ArrivalView>(point, grid, mArrivalRevision.load(),
            mResourceSystem->getVFS()->getIndexRevision(), mResourceSystem->getSceneManager()->modelSettingsRevision());
        ArrivalFootprint footprint;
        std::vector<ArrivalView::Entry> batch;
        std::size_t bytes = cached->mScenery ? cached->mScenery->mEstimatedBytes : 0;
        if (bytes > budget)
            return {};
        result->publishScenery(cached->mScenery);
        std::size_t required = 0;
        for (unsigned i = 0; i < cached->getNumEntries(); ++i)
        {
            const auto& entry = cached->getEntry(i);
            // An incomplete camera view has no continuation once it is retained indoors.
            if (!entry.mRenderingNode || !entry.mRenderingComplete)
                return {};
            const auto payload = footprint.measure(*entry.mRenderingNode);
            bytes += payload + sizeof(ArrivalView::Entry);
            if (bytes > budget)
                return {};
            required += getChunkRequest(entry, grid).mActiveGrid;
            batch.push_back({ { entry.mNode, entry.mLodFlags }, entry.mRenderingNode, payload });
        }
        if (!required)
            return {};
        result->publish(batch);
        if (result->bytes() > budget)
            return {};
        result->finish(true);
        return result;
    }

    void QuadTreeWorld::prepareView(View* view, const osg::Vec3f& viewPoint, const osg::Vec4i& grid,
        std::atomic_bool& abort, Loading::Reporter& reporter, const std::shared_ptr<ArrivalView>& arrival,
        const ArrivalView::Progress& nearReady)
    {
        Diagnostics::Scope preloadTiming("terrain", "worker", "preloadView");
        {
            Diagnostics::Loading::Timer timer(Diagnostics::Loading::Stage::QuadTree);
            ensureQuadTreeBuilt();
        }
        const float cellWorldSize = mStorage->getCellWorldSize(mWorldspace);
        const bool separateScenery = mBatchChunkManager && mBatchChunkManager->usesSceneryCoverage();

        ViewData* vd = static_cast<ViewData*>(view);
        {
            Diagnostics::Loading::Timer timer(Diagnostics::Loading::Stage::ViewSelection);
            vd->setViewPoint(viewPoint);
            vd->setActiveGrid(grid);

            DefaultLodCallback lodCallback(mLodFactor, mMinSize, mViewDistance, grid, static_cast<int>(cellWorldSize));
            mRootNode->traverseNodes(vd, viewPoint, &lodCallback);
        }

        {
            Diagnostics::Loading::Timer timer(Diagnostics::Loading::Stage::ProgressReporting);
            reporter.addTotal(vd->getNumEntries());
        }

        const auto advance = [&] {
            Diagnostics::Loading::Timer timer(Diagnostics::Loading::Stage::ProgressReporting);
            reporter.addProgress(1);
        };
        std::vector<unsigned> ordered(vd->getNumEntries());
        std::iota(ordered.begin(), ordered.end(), 0u);
        auto boundary = ordered.end();
        if (arrival)
            boundary = std::stable_partition(ordered.begin(), ordered.end(), [&](unsigned i) {
                return getChunkRequest(vd->getEntry(i), grid).mActiveGrid;
            });
        const auto near = static_cast<std::size_t>(boundary - ordered.begin());
        ArrivalFootprint footprint;
        struct NearCompileVisitor : SceneUtil::CompilePriorityVisitor
        {
            void apply(osg::Drawable& drawable) override
            {
                if (const auto* terrain = dynamic_cast<const TerrainDrawable*>(&drawable))
                    for (const auto& pass : terrain->getPasses())
                        collect(pass.get());
                SceneUtil::CompilePriorityVisitor::apply(drawable);
            }
        } nearCompile;
        auto* compile = mResourceSystem->getSceneManager()->getIncrementalCompileOperation();
        std::size_t nearCompileSets = 0;
        const auto build = [&](std::size_t first, std::size_t last) {
            constexpr std::size_t groupSize = 4;
            const bool batch = mBatchChunkManager && mBatchChunkManager->canPreloadBatches();
            for (auto i = first; i < last && !abort;)
            {
                const auto begin = i;
                std::array<ViewDataEntry*, groupSize> entries;
                std::array<ChunkRequest, groupSize> chunks;
                std::size_t count = 0;
                while (i < last && i - begin < (batch ? groupSize : 1) && !abort)
                {
                    auto& entry = vd->getEntry(ordered[i++]);
                    updateRenderingLod(entry, vd);
                    if (entry.mRenderingNode && entry.mRenderingComplete)
                        advance();
                    else
                    {
                        entries[count] = &entry;
                        chunks[count++] = getChunkRequest(entry, grid);
                    }
                }
                if (abort)
                    return;
                const auto load = [&](std::size_t index, const BatchChunkManager::Fetch& fetch) {
                    {
                        Diagnostics::Loading::Timer timer(Diagnostics::Loading::Stage::RenderNodes);
                        loadRenderingNode(*entries[index], vd, cellWorldSize, chunks[index], true, &abort,
                            fetch ? &fetch : nullptr, false, 0, separateScenery);
                    }
                    if (!abort)
                        advance();
                };
                if (batch && count)
                    mBatchChunkManager->preloadChunks(std::span(chunks).first(count), vd->getViewPoint(), abort, load);
                else if (count)
                    load(0, {});
                if (arrival && !abort)
                {
                    std::vector<ArrivalView::Entry> ready;
                    for (auto j = begin; j < i; ++j)
                    {
                        const auto& entry = vd->getEntry(ordered[j]);
                        if (entry.mRenderingNode)
                            ready.push_back({ { entry.mNode, entry.mLodFlags }, entry.mRenderingNode,
                                footprint.measure(*entry.mRenderingNode) });
                    }
                    arrival->publish(ready);
                    if (first == 0 && near != 0 && compile && Misc::useArrivalCompilePriority())
                    {
                        for (const auto& entry : ready)
                            entry.node->accept(nearCompile);
                        nearCompileSets = nearCompile.promote(*compile);
                    }
                }
            }
        };
        build(0, near);
        if (arrival && !abort)
        {
            Log(Debug::Info) << "Exterior arrival graphics priority: pendingNearSets=" << nearCompileSets
                << " enabled=" << Misc::useArrivalCompilePriority();
            nearReady(near, ordered.size() - near);
            build(near, ordered.size());
        }
        if (separateScenery && !abort)
        {
            std::vector<ChunkRequest> requests;
            for (unsigned i = 0; i < vd->getNumEntries(); ++i)
                requests.push_back(getChunkRequest(vd->getEntry(i), grid));
            vd->mScenery = mBatchChunkManager->preloadScenery(requests, viewPoint, grid, abort, reporter);
            if (arrival && !abort)
                arrival->publishScenery(vd->mScenery);
            else if (!abort)
            {
                auto prepared = createArrival(viewPoint, grid);
                prepared->publishScenery(vd->mScenery);
                prepared->finish(true);
                mPreloadedScenery.store(std::move(prepared));
            }
        }
        if (arrival && !abort)
            arrival->finish(true);
    }

    void QuadTreeWorld::reportStats(unsigned int frameNumber, osg::Stats* stats)
    {
        if (mCompositeMapRenderer)
            stats->setAttribute(
                frameNumber, "Composite", static_cast<double>(mCompositeMapRenderer->getCompileSetSize()));
    }

    void QuadTreeWorld::loadCell(int x, int y)
    {
        // fallback behavior only for undefined cells (every other is already handled in quadtree)
        const ESM::ExteriorCellLocation cellLocation(x, y, mWorldspace);
        if (mChunkManager && !mStorage->hasData(cellLocation))
            TerrainGrid::loadCell(x, y);
        else
            World::loadCell(x, y);
    }

    void QuadTreeWorld::unloadCell(int x, int y)
    {
        // fallback behavior only for undefined cells (every other is already handled in quadtree)
        const ESM::ExteriorCellLocation cellLocation(x, y, mWorldspace);
        if (mChunkManager && !mStorage->hasData(cellLocation))
            TerrainGrid::unloadCell(x, y);
        else
            World::unloadCell(x, y);
    }

    void QuadTreeWorld::addBatchChunkManager(BatchChunkManager* manager)
    {
        addChunkManager(manager);
        mBatchChunkManager = manager;
    }

    void QuadTreeWorld::addChunkManager(QuadTreeWorld::ChunkManager* m)
    {
        mChunkManagers.push_back(m);
        mTerrainRoot->setNodeMask(mTerrainRoot->getNodeMask() | m->getNodeMask());
        if (m->getViewDistance())
            m->setMaxLodLevel(
                DefaultLodCallback::convertDistanceToLodLevel(m->getViewDistance() + mViewDataMap->getReuseDistance(),
                    mMinSize, mLodFactor, ESM::getCellSize(mWorldspace)));
    }

    void QuadTreeWorld::clearAssociatedCaches()
    {
        World::clearAssociatedCaches();
        rebuildViews();
    }

    void QuadTreeWorld::rebuildViews()
    {
        ++mArrivalRevision;
        mVisibleArrival.store(nullptr);
        mPreloadedScenery.store(nullptr);
        mViewDataMap->rebuildViews();
    }

    void QuadTreeWorld::setViewDistance(float viewDistance)
    {
        if (mViewDistance == viewDistance)
            return;
        mViewDistance = viewDistance;
        rebuildViews();
    }

}
