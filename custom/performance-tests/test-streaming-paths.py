"""Execute production traversal and cache-publication bodies with counted doubles.
No renderer/driver is simulated. Real OSG readiness and queue ownership have native tests.
"""
from pathlib import Path
import os
import subprocess
import tempfile

def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

terrain = Path('components/terrain/quadtreeworld.cpp').read_text()
paging = Path('apps/openmw/mwrender/objectpaging.cpp').read_text()
view = Path('components/terrain/viewdata.hpp').read_text()
entry = view[view.index('    struct ViewDataEntry'):view.index('    class ViewData :')]
source = r'''
#include <components/sceneutil/selectionrevision.hpp>
#include <atomic>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>
#include <cstdint>
#include <iostream>
namespace osg {
// Non-owning fixture pointers. Ownership/cancellation use the real OSG native suite.
template<class T> struct ref_ptr {
 T* p=nullptr;ref_ptr()=default;ref_ptr(T* v):p(v){} template<class U>ref_ptr(ref_ptr<U> v):p(v.get()){} T* get()const{return p;}
 T* operator->()const{return p;} T& operator*()const{return *p;} explicit operator bool()const{return p!=nullptr;}
};
struct Vec2f {float a=0,b=0;float x()const{return a;}float y()const{return b;}
 bool operator<(const Vec2f& r)const{return std::tie(a,b)<std::tie(r.a,r.b);} };
struct Vec3f {float a=0,b=0,c=0;Vec3f()=default;Vec3f(float x,float y,float z):a(x),b(y),c(z){} };
struct Referenced {};
struct Node:Referenced {std::vector<Node*> children;void addChild(ref_ptr<Node> n){children.push_back(n.get());}};
struct State {unsigned id=0;unsigned getContextID()const{return id;}};
struct GraphicsContext {State state;State* getState(){return &state;}};
}
namespace osgUtil {struct IncrementalCompileOperation {
 using ContextSet=std::set<osg::GraphicsContext*>;ContextSet contexts;
 ContextSet getContextSet()const{return contexts;}
 int adds=0;void add(int,bool){++adds;}void remove(int){}
};}
namespace SceneUtil {
struct PositionAttitudeTransform:osg::Node {void setPosition(osg::Vec3f){}};
struct WorkItem {virtual ~WorkItem()=default;virtual void doWork(){}bool isDone()const{return true;}void waitTillDone(){} };
struct WorkQueue {template<class T>void cancelWorkItem(T){}template<class T>void addWorkItem(T,bool){} };
struct UnrefQueue {void push(osg::ref_ptr<osg::Referenced>){}void flush(WorkQueue&){} };
// Only routing is doubled here; real weak references/queue completion are native-tested.
struct CompileReadinessCache {
 std::set<osg::Node*> nodes;
 bool contains(osg::ref_ptr<osg::Node> n){return nodes.contains(n.get());}
 void remember(osg::ref_ptr<osg::Node> n){if(n)nodes.insert(n.get());}
 void clear(){nodes.clear();}
};
template<class Key,class Value>struct AsyncPreparation {
 struct Prepared {Value mValue{};std::shared_ptr<std::atomic_bool> mCompletion{};std::function<void()> mCancel{};std::size_t mEstimatedBytes=0;};
 struct Result {bool mReady=false,mFailed=false;Value mValue;};
 Result next;int calls=0,clears=0,polls=0,cancels=0;bool present=false;
 std::size_t cpuLimit,unresolvedLimit;
 explicit AsyncPreparation(std::shared_ptr<WorkQueue>,std::size_t cpu=2,std::size_t=64,std::size_t=0,double=0.25,double=1.0,std::size_t unresolved=0)
   :cpuLimit(cpu),unresolvedLimit(unresolved?unresolved:cpu){}
 template<class Factory>Result request(const Key&,double,Factory){++calls;present=true;return next;}
 Result poll(const Key&,double){++polls;return next;}
 bool cancel(const Key&){++cancels;return std::exchange(present,false);}
 std::size_t size()const{return present?1:0;}
 void clear(){++clears;next={};present=false;}void update(double){}
 template<class F>void cancelIf(F){}
};
struct Readiness {int mSet=0;std::shared_ptr<std::atomic_bool> mReady;std::size_t mEstimatedBytes=0;};
Readiness prepareCompileReadiness(osg::ref_ptr<osg::Node>,osgUtil::IncrementalCompileOperation::ContextSet){return {};}
}
namespace Debug {enum {Warning,Info};}
struct Log {explicit Log(int){}template<class T>Log& operator<<(const T&){return *this;}};
namespace Diagnostics {struct Scope {Scope(const char*,const char*,const char*){}};
namespace Loading {enum class Stage{ObjectChunks,SceneryPrepared,SceneryRequired,SceneryAhead,SceneryCancelled};struct Timer{Timer(Stage){}};}}
namespace Terrain {
using SceneryCell=std::pair<int,int>;
struct QuadTreeNode {osg::Vec2f getCenter()const{return {};}};
struct DefaultLodCallback {static int getNativeLodLevel(QuadTreeNode*,float){return 0;}};
''' + entry + r'''
ViewDataEntry::ViewDataEntry():mNode(nullptr),mLodFlags(0){}
struct ViewData {osg::Vec3f getViewPoint()const{return {};}};
class QuadTreeWorld {
public:
 struct ChunkRequest {float mSize=1;osg::Vec2f mCenter;unsigned mLodFlags=0;bool mActiveGrid=false;};
 struct ChunkManager {
  struct AsyncResult {bool mSupported=false,mReady=false;osg::ref_ptr<osg::Node> mNode;};
  AsyncResult result;int synchronous=0,asynchronous=0;osg::Node node;
  bool lastQueue=true;
  bool separate=false;bool usesSceneryCoverage()const{return separate;}
  AsyncResult requestChunk(const ChunkRequest&,osg::Vec3f,double,bool queue=true){++asynchronous;lastQueue=queue;return result;}
  osg::ref_ptr<osg::Node> getChunk(float,osg::Vec2f,unsigned char,unsigned,bool,osg::Vec3f,bool,const std::atomic_bool*){
   ++synchronous;return &node;}
 };
 struct BatchChunkManager:ChunkManager {using Fetch=std::function<osg::ref_ptr<osg::Node>()>;};
 float mMinSize=1;std::vector<ChunkManager*> mChunkManagers;BatchChunkManager* mBatchChunkManager=nullptr;
 osg::ref_ptr<ChunkManager> mChunkManager;
 void loadRenderingNode(ViewDataEntry&,ViewData*,float,const ChunkRequest&,bool,const std::atomic_bool*,const BatchChunkManager::Fetch*,bool,double,bool=false,bool=false);
};
''' + function(terrain, '    void QuadTreeWorld::loadRenderingNode') + r'''
}
using ChunkId=std::tuple<osg::Vec2f,float,bool>;
struct Cache {std::map<ChunkId,osg::ref_ptr<osg::Node>> values;
 osg::ref_ptr<osg::Node> getRefFromObjectCache(const ChunkId& id){return values[id];}
 void addEntryToObjectCache(const ChunkId& id,osg::Node* node){values[id]=node;}
 void clear(){values.clear();}
};
namespace Resource {template<class T>struct NodeResourceManager {Cache cache;Cache* mCache=&cache;void clearCache(){mCache->clear();}};}
struct VFS {std::uint64_t revision=0;auto getIndexRevision()const{return revision;}};
struct Shader {std::uint64_t revision=0;auto compileRevision()const{return revision;}};
struct Scene {std::uint64_t revision=0;VFS vfs;Shader shader;osgUtil::IncrementalCompileOperation* operation=nullptr;
 auto modelSettingsRevision()const{return revision;}VFS* getVFS(){return &vfs;}
 Shader& getShaderManager(){return shader;}
 auto* getIncrementalCompileOperation(){return operation;}};
class ObjectPaging:public Resource::NodeResourceManager<ChunkId> {
public:
 using ChunkRequest=Terrain::QuadTreeWorld::ChunkRequest;
 using AsyncResult=Terrain::QuadTreeWorld::ChunkManager::AsyncResult;
 struct AsyncState;std::unique_ptr<AsyncState> mAsync;
 struct SceneryBridge {};
 struct SceneryState {std::vector<osg::ref_ptr<osg::Referenced>> mRetired;std::size_t mRetiredBytes=0,mQueuedRetiredBytes=0;unsigned mCompileGeneration=0;void clear(){} };
 std::unique_ptr<SceneryState> mScenery=std::make_unique<SceneryState>();
 using CacheEpoch=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
 struct Selection {std::uint64_t mRevision=0,mVfsRevision=0;CacheEpoch mEpoch{};};
 struct Operation {int adds=0;void add(int,bool){++adds;}} operation;
 struct PendingCompile {int mSet=0;Operation* mOperation=nullptr;};
 std::atomic_uint64_t mSelectionRevision{0},mGraphicsRevision{0};
 SceneUtil::SelectionRevision mSelectionRevisions;
 std::uint64_t selectionRevision(const ChunkId&);std::uint64_t selectionRevisionLocked(const ChunkId&)const;
 bool usesSceneryCoverage()const{return false;}
 Scene scene;Scene* mSceneManager=&scene;std::mutex mRefTrackerMutex;CacheEpoch mCacheEpoch{};bool mActiveGrid=true;
 int builds=0;std::function<void()> duringCreate;
 CacheEpoch cacheEpoch()const;void refreshCacheEpoch();void clearCache();
 osg::ref_ptr<osg::Node> getCachedChunk(const ChunkId&);
 bool updateAsyncState(double);bool canPrepareAsync()const;AsyncResult requestChunk(const ChunkRequest&,const osg::Vec3f&,double,bool=true);
 Selection selectChunk(const ChunkRequest& r,const osg::Vec3f&,const std::atomic_bool*){return {selectionRevision({r.mCenter,r.mSize,r.mActiveGrid}),scene.vfs.revision,cacheEpoch()};}
 osg::ref_ptr<osg::Node> createChunk(const ChunkRequest&,const osg::Vec3f&,const Selection&,bool,const std::atomic_bool*,bool,PendingCompile& p){
  ++builds;if(auto f=std::exchange(duringCreate,{}))f();p.mSet=builds;p.mOperation=&operation;return new osg::Node;}
 osg::ref_ptr<osg::Node> fetchChunk(const ChunkRequest&,const osg::Vec3f&,bool,const std::atomic_bool*,const Selection*,bool);
};
''' + function(paging, '    struct ObjectPaging::AsyncState') + ';\n' + '\n'.join(function(paging, s) for s in [
    '    bool ObjectPaging::updateAsyncState(',
    '    bool ObjectPaging::canPrepareAsync()',
    '    ObjectPaging::AsyncResult ObjectPaging::requestChunk(',
    '    std::uint64_t ObjectPaging::selectionRevision(',
    '    std::uint64_t ObjectPaging::selectionRevisionLocked(',
    '    ObjectPaging::CacheEpoch ObjectPaging::cacheEpoch()',
    '    void ObjectPaging::refreshCacheEpoch()',
    '    osg::ref_ptr<osg::Node> ObjectPaging::getCachedChunk(',
    '    void ObjectPaging::clearCache()',
    '    osg::ref_ptr<osg::Node> ObjectPaging::fetchChunk(']) + r'''
int main(){
 using namespace Terrain;
 QuadTreeWorld world;QuadTreeWorld::ChunkManager ground;QuadTreeWorld::BatchChunkManager objects;
 world.mChunkManagers={&ground,&objects};world.mBatchChunkManager=&objects;world.mChunkManager=&ground;
 ViewData view;QuadTreeNode tile;ViewDataEntry entry;entry.mNode=&tile;QuadTreeWorld::ChunkRequest request;
 auto load=[&](ViewDataEntry& target,bool async){world.loadRenderingNode(target,&view,8192,request,false,nullptr,nullptr,async,1);};
 objects.result={true,false,{}};load(entry,true);
 assert(entry.mRenderingComplete && entry.mRenderingReadyForCamera && entry.mRenderingNode->children.size()==2);
 assert(objects.synchronous==1 && !objects.lastQueue && entry.mRenderingNode->children.front()==&ground.node);
 auto old=entry.mRenderingNode;entry.mRenderingNode=nullptr;
 objects.result={true,true,&objects.node};load(entry,true);
 assert(entry.mRenderingComplete && entry.mRenderingReadyForCamera && entry.mRenderingNode->children.size()==2);
 assert(objects.synchronous==1 && !objects.lastQueue && old->children.size()==2);
 ViewDataEntry ray;ray.mNode=&tile;load(ray,false);
 assert(ray.mRenderingComplete && !ray.mRenderingReadyForCamera && objects.synchronous==2);
 objects.result={true,false,{}};auto camera=ray;load(camera,true);
 assert(camera.mRenderingComplete && camera.mRenderingReadyForCamera && camera.mRenderingNode->children.size()==2);
 assert(objects.synchronous==3 && ray.mRenderingNode->children.size()==2 && !ray.mRenderingReadyForCamera);
 ViewDataEntry active;active.mNode=&tile;request.mActiveGrid=true;load(active,true);
 assert(active.mRenderingComplete && objects.synchronous==4);request.mActiveGrid=false;
 ViewDataEntry compatibility;compatibility.mNode=&tile;objects.result={};load(compatibility,true);
 assert(compatibility.mRenderingComplete && objects.synchronous==5);
 ViewDataEntry empty;empty.mNode=&tile;objects.result={true,true,{}};load(empty,true);
 assert(empty.mRenderingComplete && empty.mRenderingNode->children.size()==1 && objects.synchronous==5);
 // Ground fallback must skip object construction and remain incomplete for an exact query.
 ViewDataEntry fallback;fallback.mNode=&tile;
 world.loadRenderingNode(fallback,&view,8192,request,false,nullptr,nullptr,false,1,false,true);
 assert(fallback.mRenderingGroundFallback && fallback.mRenderingNode->children.size()==1);
 assert(fallback.mRenderingNode->children.front()==&ground.node && objects.synchronous==5);
 assert(fallback.needsRendering(false));
 load(fallback,false);
 assert(!fallback.mRenderingGroundFallback && fallback.mRenderingNode->children.size()==2 && objects.synchronous==6);
 std::atomic_bool abort{true};ViewDataEntry stopped;stopped.mNode=&tile;
 world.loadRenderingNode(stopped,&view,8192,request,false,&abort,nullptr,true,1);assert(!stopped.mRenderingNode);
 for(int kind=0;kind<3;++kind){
  ObjectPaging paging;
  paging.duringCreate=[&]{if(kind==0)paging.clearCache();else if(kind==1)++paging.scene.revision;else ++paging.scene.vfs.revision;};
  auto node=paging.fetchChunk(request,{},false,nullptr,nullptr,false);
  assert(node && paging.builds==2 && paging.operation.adds==1);
  assert(paging.fetchChunk(request,{},false,nullptr,nullptr,false).get()==node.get() && paging.builds==2);
  ++paging.scene.revision;
  assert(!paging.getCachedChunk({{},1,false}));
 }
 // Execute the production publication guard while an unrelated/owned cell changes.
 for(bool affected:{false,true}){
  ObjectPaging paging;
  paging.duringCreate=[&]{paging.mSelectionRevisions.invalidate(affected?-1:100,affected?-1:100,false);};
  auto node=paging.fetchChunk(request,{},false,nullptr,nullptr,false);
  assert(node && paging.builds==(affected?2:1) && paging.operation.adds==1);
 }
 {
  ObjectPaging paging;auto queue=std::make_shared<SceneUtil::WorkQueue>();
  osgUtil::IncrementalCompileOperation operation,otherOperation;
  osg::GraphicsContext context,otherContext;operation.contexts={&context};
  paging.scene.operation=&operation;paging.mAsync=std::make_unique<ObjectPaging::AsyncState>(queue);
  assert(!paging.updateAsyncState(0));
  const ChunkId key{{},1,false};osg::Node original,replacement;
  auto& jobs=paging.mAsync->mRequests;auto& completed=paging.mAsync->mCompleted;
  assert(jobs.cpuLimit==2 && jobs.unresolvedLimit==8);
  assert(paging.mAsync->mBridges.cpuLimit==2 && paging.mAsync->mBridges.unresolvedLimit==2);
  auto poll=[&]{return paging.requestChunk(request,{},1);};
  // A required miss polls/cancels only; it must never enqueue a duplicate before demand work.
  assert(!paging.requestChunk(request,{},1,false).mReady && jobs.calls==0 && jobs.polls==1);
  jobs.present=true;assert(!paging.requestChunk(request,{},1,false).mReady && !jobs.present && jobs.calls==0);
  paging.cache.values[key]=&original;
  const int cancellations=jobs.cancels;
  assert(!paging.requestChunk(request,{},1,false).mReady && jobs.calls==0 && jobs.cancels==cancellations);
  assert(!completed.contains(&original)); // cached CPU data is not a graphics receipt
  jobs.next={false,false,&original};assert(!poll().mReady && !completed.contains(&original));
  jobs.next={true,false,&original};assert(poll().mReady && completed.contains(&original));
  // Rebuilt wrappers after retired queue records reuse the current completed node.
  jobs.clear();int calls=jobs.calls;auto ready=poll();
  assert(ready.mReady && ready.mNode.get()==&original && jobs.calls==calls);
  assert(paging.requestChunk(request,{},1,false).mReady && jobs.calls==calls);
  ++paging.mSelectionRevision;paging.mSelectionRevisions.invalidate(100,100,false);
  const int originalClears=jobs.clears;assert(!paging.updateAsyncState(2));assert(jobs.clears==originalClears);
  assert(completed.contains(&original) && poll().mReady && jobs.calls==calls);
  // Selective CPU-cache invalidation must not transfer proof to a replacement.
  paging.cache.values[key]=&replacement;
  assert(!poll().mReady && jobs.calls==calls+1 && !completed.contains(&replacement));
  request.mActiveGrid=true;assert(!poll().mSupported);request.mActiveGrid=false;
  // Every non-selection invalidator retires both jobs and graphics receipts.
  for(int kind=0;kind<7;++kind){
   completed.remember(&original);const int clears=jobs.clears;
   if(kind==0)++paging.scene.vfs.revision;
   if(kind==1)++paging.scene.revision;
   if(kind==2)++paging.mGraphicsRevision;
   if(kind==3)++context.state.id;
   if(kind==4)operation.contexts.insert(&otherContext);
   if(kind==5){otherOperation.contexts=operation.contexts;paging.scene.operation=&otherOperation;}
   if(kind==6)++paging.scene.shader.revision;
   assert(paging.updateAsyncState(3+kind));
   assert(!completed.contains(&original) && jobs.clears==clears+1);
  }
  paging.scene.operation=nullptr;assert(paging.updateAsyncState(10));assert(!poll().mSupported);
 }
 std::cout<<"PASS: production traversal completes required camera scenery on pending misses without new async jobs; prepared/empty results, rays, active/compatibility paths and aborts retain semantics; publication rejects clear/model/VFS changes during assembly\n";
 std::cout<<"PASS: production requests reuse completed current nodes without new jobs; pending/replaced nodes and all graphics generations retain preparation gates\n";
}
'''
def run(source, name):
    with tempfile.TemporaryDirectory() as tmp:
        cpp=Path(tmp)/(name+'.cpp');cpp.write_text(source)
        exe=Path(tmp)/(name+('.exe' if os.name=='nt' else ''))
        if os.name=='nt':
            command=['cl','/nologo','/std:c++20','/EHsc','/I'+str(Path.cwd()),str(cpp),'/Fe:'+str(exe),'/Fo:'+str(Path(tmp)/(name+'.obj'))]
        else:
            command=['c++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I'+str(Path.cwd()),str(cpp),'-o',str(exe)]
        subprocess.run(command,check=True)
        subprocess.run([str(exe)],env=dict(os.environ,OPENMW_PREPARED_CELLS='0',OPENMW_STAGED_ARRIVAL='0'),check=True)

run(source, 'streaming')

# Exercise the actual process switches in fresh processes (the production helpers
# cache their values). Default play must not select unmerged experimental cover.
policy = r"""
#include <components/misc/combinedperformance.hpp>
#include <iostream>
int main(){std::cout<<Misc::useAsyncObjectPaging()<<Misc::useSceneryCoverage();}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/'policy.cpp';cpp.write_text(policy)
    exe=Path(directory)/('policy.exe' if os.name=='nt' else 'policy')
    if os.name=='nt':
        command=['cl','/nologo','/std:c++20','/EHsc','/I'+str(Path.cwd()),str(cpp),'/Fe:'+str(exe),'/Fo:'+str(Path(directory)/'policy.obj')]
    else:
        command=['c++','-std=c++20','-I'+str(Path.cwd()),str(cpp),'-o',str(exe)]
    subprocess.run(command,check=True)
    clean={k:v for k,v in os.environ.items() if k not in ('OPENMW_SCENERY_COVERAGE','OPENMW_ASYNC_OBJECT_PAGING','OPENMW_COMBINED_PERFORMANCE')}
    for extra,expected in [({},'10'),({'OPENMW_SCENERY_COVERAGE':'1'},'11'),({'OPENMW_ASYNC_OBJECT_PAGING':'0','OPENMW_SCENERY_COVERAGE':'1'},'00'),({'OPENMW_COMBINED_PERFORMANCE':'0'},'00')]:
        assert subprocess.check_output([str(exe)],env=dict(clean,**extra),text=True)==expected
print('PASS: default complete optimized scenery, explicit experimental cover opt-in and independent rollback switches')

# Runtime destinations must not replace source-cell identity in paging revisions.
rendering = Path('apps/openmw/mwrender/renderingmanager.cpp').read_text()
cells = Path('apps/openmw/mwworld/cellstore.cpp').read_text()
source = r"""
#include <components/sceneutil/selectionrevision.hpp>
#include <cassert>
#include <map>
#include <iostream>
namespace osg {struct Vec2i{int x,y;Vec2i(int a,int b):x(a),y(b){}};}
namespace ESM {struct RefNum {bool hasContentFile()const{return true;}};}
namespace MWWorld {
struct LiveCellRefBase{};
struct CellStore;
struct Position {int asVec3()const{return 0;}};
struct Ref {ESM::RefNum getRefNum()const{return {};}Position getPosition()const{return {};}};
struct ConstPtr {const LiveCellRefBase* base;const CellStore* cell;
 bool isInCell()const{return cell!=nullptr;}auto getBase()const{return base;}
 auto getCell()const{return cell;}Ref getCellRef()const{return {};}};
struct Cell {int world,x,y;int getWorldSpace()const{return world;}int getGridX()const{return x;}int getGridY()const{return y;}};
struct CellStore {Cell value;bool exterior;std::map<LiveCellRefBase*,CellStore*> mMovedHere;
 bool isExterior()const{return exterior;}const Cell* getCell()const{return &value;}
 const CellStore* getOriginCell(const ConstPtr&)const;};
}
""" + function(cells, '    const CellStore* MWWorld::CellStore::getOriginCell(').replace(
    'const CellStore* MWWorld::CellStore::', 'const MWWorld::CellStore* MWWorld::CellStore::') + r"""
struct Pager {SceneUtil::SelectionRevision revisions;int calls=0;
 bool enableObject(int,ESM::RefNum,int,osg::Vec2i cell,bool){++calls;revisions.invalidate(cell.x,cell.y,false);return true;}
 bool blacklistObject(int,ESM::RefNum,int,osg::Vec2i cell){++calls;revisions.invalidate(cell.x,cell.y,true);return true;}};
struct Terrain {void rebuildViews(){}};
struct Chunks {Pager pager;Terrain terrain;Pager* mObjectPaging=&pager;Terrain* mTerrain=&terrain;};
struct RenderingManager {std::map<int,Chunks> worlds;
 Chunks& getWorldspaceChunkMgr(int world){return worlds[world];}
 bool pagingEnableObject(int,const MWWorld::ConstPtr&,bool);
 void pagingBlacklistObject(int,const MWWorld::ConstPtr&);};
""" + function(rendering,'    bool RenderingManager::pagingEnableObject(') + function(
    rendering,'    void RenderingManager::pagingBlacklistObject(') + r"""
int main(){
 MWWorld::LiveCellRefBase object;
 MWWorld::CellStore origin{{5,-2,-3},true,{}}, destination{{9,30,40},true,{}}, interior{{0,0,0},false,{}};
 destination.mMovedHere[&object]=&origin;interior.mMovedHere[&object]=&origin;
 RenderingManager manager;
 for(const auto* current:{&destination,&interior}){
  auto& pager=manager.worlds[5].pager;
  auto old=pager.revisions.get(-1.5f,-2.5f,1,false);
  assert(manager.pagingEnableObject(0,{&object,current},false));
  assert(pager.revisions.get(-1.5f,-2.5f,1,false)>old);
  assert(pager.revisions.get(30.5f,40.5f,1,false)==0);
  manager.pagingBlacklistObject(0,{&object,current});
 }
 assert(manager.worlds.size()==1 && manager.worlds[5].pager.calls==4);
 destination.mMovedHere[&object]=&interior; // an interior source has no distant cell ownership
 assert(!manager.pagingEnableObject(0,{&object,&destination},true));
 assert(manager.worlds.size()==1 && manager.worlds[5].pager.calls==4);
 std::cout<<"PASS: production paging routes moved refs to source exterior cell/worldspace, including interior arrivals; destination revisions cannot hide stale source publication\n";
}
"""
run(source,'moved-source')

# Execute the actual future-view selector and arrival worker. The quadtree and
# reporter are controlled doubles; native tests exercise real prediction math,
# queue cancellation and graphics readiness. This verifies routing/ordering only.
source = r'''
#include <components/misc/combinedperformance.hpp>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>
namespace osg {
struct Vec2f {float a=0,b=0;float x()const{return a;}float y()const{return b;}
 Vec2f operator*(float v)const{return {a*v,b*v};}Vec2f operator-(Vec2f v)const{return {a-v.a,b-v.b};}
 float length2()const{return a*a+b*b;}bool operator<(Vec2f v)const{return std::tie(a,b)<std::tie(v.a,v.b);} };
struct Vec3f {float a=0,b=0,c=0;float x()const{return a;}float y()const{return b;}float z()const{return c;}
 Vec3f operator-(Vec3f v)const{return {a-v.a,b-v.b,c-v.c};}float length2()const{return a*a+b*b+c*c;} };
struct Vec4i {int a=0,b=0,c=0,d=0;int x()const{return a;}int y()const{return b;}int z()const{return c;}int w()const{return d;}
 bool operator==(const Vec4i&)const=default;};
template<class T>struct ref_ptr {T* p=nullptr;ref_ptr(T* v):p(v){}operator T*()const{return p;}};
}
namespace Diagnostics::Loading {enum class Stage{SceneryAhead};struct Timer {explicit Timer(Stage){}};}
namespace SceneUtil {struct WorkItem {virtual ~WorkItem()=default;virtual void abort()=0;virtual void doWork()=0;};}
namespace Debug {enum {Info,Warning};}struct Log {explicit Log(int){}template<class T>Log& operator<<(const T&){return *this;}};
namespace Loading {
struct Listener {};
struct Reporter {bool done=false;void complete(){done=true;}void wait(Listener&){assert(done);}};
}
struct PositionCellGrid {osg::Vec3f mPosition;osg::Vec4i mCellBounds;};
namespace Terrain {
struct StreamingLookAhead {
 struct Plan {osg::Vec3f mCurrent,mPredicted;osg::Vec4i mPredictedGrid;float mMargin;};
 std::optional<Plan> next;
 auto update(const osg::Vec3f&,const osg::Vec4i&,double,float){return next;}
};
struct QuadTreeNode {osg::Vec2f center;float size=1;unsigned flags=0;
 auto getCenter()const{return center;}float getSize()const{return size;}};
struct ViewDataEntry {QuadTreeNode* mNode;unsigned mLodFlags=0;};
struct ViewData {
 std::vector<ViewDataEntry> entries;StreamingLookAhead mStreamingLookAhead;osg::Vec3f point;osg::Vec4i grid;
 unsigned getNumEntries()const{return entries.size();}ViewDataEntry& getEntry(unsigned i){return entries[i];}
 void setViewPoint(osg::Vec3f v){point=v;}void setActiveGrid(osg::Vec4i v){grid=v;}
};
struct DefaultLodCallback {
 float distance;osg::Vec4i grid;int cellSize;
 DefaultLodCallback(float,float,float d,osg::Vec4i g,int c):distance(d),grid(g),cellSize(c){}
};
struct Root {std::function<void(ViewData*,osg::Vec3f,DefaultLodCallback*)> select;
 void traverseNodes(ViewData* v,osg::Vec3f p,DefaultLodCallback* lod){select(v,p,lod);}};
class QuadTreeWorld {
public:
 struct ChunkRequest {float mSize;osg::Vec2f mCenter;unsigned mLodFlags;bool mActiveGrid;};
 struct BatchChunkManager {
  struct Call {ChunkRequest request;osg::Vec3f point;double time;bool queue;};std::vector<Call> calls;bool supported=true;bool canPrepareAsync()const{return supported;}
  void requestChunk(ChunkRequest r,osg::Vec3f p,double t,bool q){calls.push_back({r,p,t,q});}
  void prepareScenery(ChunkRequest r,osg::Vec3f p,double t){requestChunk(r,p,t,true);}
 };
 BatchChunkManager* mBatchChunkManager=nullptr;Root* mRootNode=nullptr;osg::Vec4i mActiveGrid;
 float mLodFactor=1,mMinSize=1;std::atomic<float> mViewDistance{100};
 void updateRenderingLod(ViewDataEntry& e,ViewData*){e.mLodFlags=e.mNode->flags;}
 ChunkRequest getChunkRequest(const ViewDataEntry&,const osg::Vec4i&)const;
 void prepareAhead(ViewData&,const osg::Vec3f&,double,float);
};
''' + function(terrain, '    QuadTreeWorld::ChunkRequest QuadTreeWorld::getChunkRequest') + '\n' + function(
    terrain, '    void QuadTreeWorld::prepareAhead') + r'''
struct View {};
struct ArrivalView {bool completed=false;int failed=0,mRevision=0;
 // This extracted worker harness retains the legacy-mode assertions below;
 // the native StagedArrival suite exercises early readiness and cancellation.
 bool activeReady()const{assert(false);return false;}void cancel(){assert(false);}
 bool complete()const{return completed;}void finish(bool ok){completed=ok;if(!ok)++failed;}
 bool matches(osg::Vec3f,osg::Vec4i,float,int)const{return true;}std::size_t bytes()const{return 42;}};
struct World {
 View view;int mode=0;bool valid=true;std::function<void(Loading::Reporter&)> duringNear;
 std::unique_ptr<View> createView(){return std::make_unique<View>();}bool validArrival(const ArrivalView&)const{return valid;}
 template<class F>void preloadArrival(View*,const std::shared_ptr<ArrivalView>& result,
  std::atomic_bool& abort,Loading::Reporter& reporter,F near){
  near(9,40);duringNear(reporter);
  if(mode==1){abort=true;return;}if(mode==2)throw std::runtime_error("expected");
  if(mode==3)throw 42;
  if(mode==4)return;
  result->finish(true);if(mode==5)valid=false;
 }
};
}
''' + function(Path('apps/openmw/mwworld/cellpreloader.cpp').read_text(), '    class ExteriorArrivalJob :') + r''';
int main(){
 using namespace Terrain;
 QuadTreeWorld world;QuadTreeWorld::BatchChunkManager batch;Root root;ViewData current;
 world.mBatchChunkManager=&batch;world.mRootNode=&root;world.mActiveGrid={-1,-1,1,1};
 QuadTreeNode visible{{3,3},1,7},active{{.5f,.5f}},future{{2.5f,0}},outer{{4.5f,0}},other{{3.5f,0}};
 QuadTreeNode outerOtherFlags{{4.5f,0},1,29};
 current.entries={{&visible,0}};current.mStreamingLookAhead.next=StreamingLookAhead::Plan{{0,0,0},{5,0,0},{0,-1,2,1},10};
 int selections=0;
 root.select=[&](ViewData* target,osg::Vec3f point,DefaultLodCallback* lod){
  ++selections;assert(lod->distance==110 && lod->cellSize==10);
  if(point.x()==5){assert((lod->grid==osg::Vec4i{0,-1,2,1}));target->entries={{&visible},{&active},{&outer},{&future}};}
  else{assert((lod->grid==world.mActiveGrid));target->entries={{&outerOtherFlags},{&other}};}
 };
 world.prepareAhead(current,{},1,10);
 assert(selections==2 && batch.calls.size()==3 && current.entries.size()==1);
 assert(batch.calls[0].request.mCenter.x()==2.5f && batch.calls[0].point.x()==5);
 assert(batch.calls[1].request.mCenter.x()==3.5f && batch.calls[1].point.x()==0);
 assert(batch.calls[2].request.mCenter.x()==4.5f); // duplicate across predictions submitted once
 for(const auto& c:batch.calls)assert(c.queue && !c.request.mActiveGrid && c.time==1);
 batch.calls.clear();std::vector<QuadTreeNode> many(300);
 for(unsigned i=0;i<many.size();++i)many[i].center={float(i+2),5};
 current.mStreamingLookAhead.next=StreamingLookAhead::Plan{{},{},world.mActiveGrid,10};
 root.select=[&](ViewData* v,osg::Vec3f,DefaultLodCallback*){++selections;for(auto& node:many)v->entries.push_back({&node});};
 world.prepareAhead(current,{},2,10);assert(selections==3 && batch.calls.size()==256);
 assert(batch.calls.front().request.mCenter.x()==2 && batch.calls.back().request.mCenter.x()==257);
 batch.supported=false;world.prepareAhead(current,{},3,10);assert(selections==3);
 batch.supported=true;current.mStreamingLookAhead.next.reset();world.prepareAhead(current,{},4,10);assert(selections==3);
 for(int mode=0;mode<6;++mode){
  World terrainWorld;terrainWorld.mode=mode;auto arrival=std::make_shared<ArrivalView>();
  ExteriorArrivalJob job(&terrainWorld,arrival);Loading::Listener listener;
  terrainWorld.duringNear=[&](Loading::Reporter& progress){assert(!job.finishedVisible() && !progress.done);};
  job.doWork();assert(job.waitVisible(listener)==(mode==0));
  assert(job.finishedVisible()==(mode==0));
  if(mode>0 && mode<5)assert(arrival->failed==1);
 }
 std::cout<<"PASS: production look-ahead selects predicted LOD/grid plus outer radius, skips visible/active chunks, deduplicates and bounds ordered submission; no current view mutation\n";
 std::cout<<"PASS: production arrival waits through distant CPU preparation; active-ready callback cannot release loading; abort/failure/stale completion release safely without a graphics wait\n";
}
'''
run(source, 'first_scenery')
