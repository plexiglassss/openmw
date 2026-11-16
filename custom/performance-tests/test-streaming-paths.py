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
 T* operator->()const{return p;} explicit operator bool()const{return p!=nullptr;}
};
struct Vec2f {float a=0,b=0;float x()const{return a;}float y()const{return b;}
 bool operator<(const Vec2f& r)const{return std::tie(a,b)<std::tie(r.a,r.b);} };
struct Vec3f {float a=0,b=0,c=0;Vec3f()=default;Vec3f(float x,float y,float z):a(x),b(y),c(z){} };
struct Node {std::vector<Node*> children;void addChild(ref_ptr<Node> n){children.push_back(n.get());}};
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
struct WorkQueue {};
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
 Result next;int calls=0,clears=0;explicit AsyncPreparation(WorkQueue*){}
 template<class Factory>Result request(const Key&,double,Factory){++calls;return next;}
 void clear(){++clears;next={};}void update(double){}
};
struct Readiness {int mSet=0;std::shared_ptr<std::atomic_bool> mReady;std::size_t mEstimatedBytes=0;};
Readiness prepareCompileReadiness(osg::ref_ptr<osg::Node>,osgUtil::IncrementalCompileOperation::ContextSet){return {};}
}
namespace Debug {enum {Warning};}
struct Log {explicit Log(int){}template<class T>Log& operator<<(const T&){return *this;}};
namespace Terrain {
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
  AsyncResult requestChunk(const ChunkRequest&,osg::Vec3f,double){++asynchronous;return result;}
  osg::ref_ptr<osg::Node> getChunk(float,osg::Vec2f,unsigned char,unsigned,bool,osg::Vec3f,bool,const std::atomic_bool*){
   ++synchronous;return &node;}
 };
 struct BatchChunkManager:ChunkManager {using Fetch=std::function<osg::ref_ptr<osg::Node>()>;};
 float mMinSize=1;std::vector<ChunkManager*> mChunkManagers;BatchChunkManager* mBatchChunkManager=nullptr;
 void loadRenderingNode(ViewDataEntry&,ViewData*,float,const ChunkRequest&,bool,const std::atomic_bool*,const BatchChunkManager::Fetch*,bool,double);
};
''' + function(terrain, '    void QuadTreeWorld::loadRenderingNode') + r'''
}
namespace Diagnostics {struct Scope {Scope(const char*,const char*,const char*){}};
namespace Loading {enum class Stage{ObjectChunks};struct Timer{Timer(Stage){}};}}
using ChunkId=std::tuple<osg::Vec2f,float,bool>;
struct Cache {std::map<ChunkId,osg::ref_ptr<osg::Node>> values;
 osg::ref_ptr<osg::Node> getRefFromObjectCache(const ChunkId& id){return values[id];}
 void addEntryToObjectCache(const ChunkId& id,osg::Node* node){values[id]=node;}
 void clear(){values.clear();}
};
namespace Resource {template<class T>struct GenericResourceManager {Cache cache;Cache* mCache=&cache;void clearCache(){mCache->clear();}};}
struct VFS {std::uint64_t revision=0;auto getIndexRevision()const{return revision;}};
struct Shader {std::uint64_t revision=0;auto compileRevision()const{return revision;}};
struct Scene {std::uint64_t revision=0;VFS vfs;Shader shader;osgUtil::IncrementalCompileOperation* operation=nullptr;
 auto modelSettingsRevision()const{return revision;}VFS* getVFS(){return &vfs;}
 Shader& getShaderManager(){return shader;}
 auto* getIncrementalCompileOperation(){return operation;}};
class ObjectPaging:public Resource::GenericResourceManager<ChunkId> {
public:
 using ChunkRequest=Terrain::QuadTreeWorld::ChunkRequest;
 using AsyncResult=Terrain::QuadTreeWorld::ChunkManager::AsyncResult;
 struct AsyncState;std::unique_ptr<AsyncState> mAsync;
 using CacheEpoch=std::tuple<std::uint64_t,std::uint64_t,std::uint64_t>;
 struct Selection {std::uint64_t mRevision=0,mVfsRevision=0;CacheEpoch mEpoch{};};
 struct Operation {int adds=0;void add(int,bool){++adds;}} operation;
 struct PendingCompile {int mSet=0;Operation* mOperation=nullptr;};
 std::atomic_uint64_t mSelectionRevision{0},mGraphicsRevision{0};
 Scene scene;Scene* mSceneManager=&scene;std::mutex mRefTrackerMutex;CacheEpoch mCacheEpoch{};bool mActiveGrid=true;
 int builds=0;std::function<void()> duringCreate;
 CacheEpoch cacheEpoch()const;void refreshCacheEpoch();void clearCache();
 osg::ref_ptr<osg::Node> getCachedChunk(const ChunkId&);
 bool updateAsyncState(double);AsyncResult requestChunk(const ChunkRequest&,const osg::Vec3f&,double);
 Selection selectChunk(const ChunkRequest&,const osg::Vec3f&,const std::atomic_bool*){return {mSelectionRevision.load(),scene.vfs.revision,cacheEpoch()};}
 osg::ref_ptr<osg::Node> createChunk(const ChunkRequest&,const osg::Vec3f&,const Selection&,bool,const std::atomic_bool*,bool,PendingCompile& p){
  ++builds;if(auto f=std::exchange(duringCreate,{}))f();p.mSet=builds;p.mOperation=&operation;return new osg::Node;}
 osg::ref_ptr<osg::Node> fetchChunk(const ChunkRequest&,const osg::Vec3f&,bool,const std::atomic_bool*,const Selection*,bool);
};
''' + function(paging, '    struct ObjectPaging::AsyncState') + ';\n' + '\n'.join(function(paging, s) for s in [
    '    bool ObjectPaging::updateAsyncState(',
    '    ObjectPaging::AsyncResult ObjectPaging::requestChunk(',
    '    ObjectPaging::CacheEpoch ObjectPaging::cacheEpoch()',
    '    void ObjectPaging::refreshCacheEpoch()',
    '    osg::ref_ptr<osg::Node> ObjectPaging::getCachedChunk(',
    '    void ObjectPaging::clearCache()',
    '    osg::ref_ptr<osg::Node> ObjectPaging::fetchChunk(']) + r'''
int main(){
 using namespace Terrain;
 QuadTreeWorld world;QuadTreeWorld::ChunkManager ground;QuadTreeWorld::BatchChunkManager objects;
 world.mChunkManagers={&ground,&objects};world.mBatchChunkManager=&objects;
 ViewData view;QuadTreeNode tile;ViewDataEntry entry;entry.mNode=&tile;QuadTreeWorld::ChunkRequest request;
 auto load=[&](ViewDataEntry& target,bool async){world.loadRenderingNode(target,&view,8192,request,false,nullptr,nullptr,async,1);};
 objects.result={true,false,{}};load(entry,true);
 assert(!entry.mRenderingComplete && entry.mRenderingNode->children.size()==1 && objects.synchronous==0);
 assert(entry.mRenderingNode->children.front()==&ground.node);
 auto old=entry.mRenderingNode;objects.result={true,true,&objects.node};load(entry,true);
 assert(entry.mRenderingComplete && entry.mRenderingReadyForCamera && entry.mRenderingNode->children.size()==2);
 assert(old->children.size()==1); // previously shared fallback remains immutable
 ViewDataEntry ray;ray.mNode=&tile;ray.mRenderingNode=old;ray.mRenderingComplete=false;load(ray,false);
 assert(ray.mRenderingComplete && !ray.mRenderingReadyForCamera && objects.synchronous==1);
 objects.result={true,false,{}};auto camera=ray;load(camera,true);
 assert(!camera.mRenderingComplete && camera.mRenderingNode->children.size()==1);
 assert(ray.mRenderingNode->children.size()==2); // exact ray result is not an implicit graphics fence
 ViewDataEntry active;active.mNode=&tile;request.mActiveGrid=true;load(active,true);
 assert(active.mRenderingComplete && objects.synchronous==2);request.mActiveGrid=false;
 ViewDataEntry compatibility;compatibility.mNode=&tile;objects.result={};load(compatibility,true);
 assert(compatibility.mRenderingComplete && objects.synchronous==3);
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
 {
  ObjectPaging paging;SceneUtil::WorkQueue queue;
  osgUtil::IncrementalCompileOperation operation,otherOperation;
  osg::GraphicsContext context,otherContext;operation.contexts={&context};
  paging.scene.operation=&operation;paging.mAsync=std::make_unique<ObjectPaging::AsyncState>(&queue);
  assert(!paging.updateAsyncState(0));
  const ChunkId key{{},1,false};osg::Node original,replacement;
  auto& jobs=paging.mAsync->mRequests;auto& completed=paging.mAsync->mCompleted;
  auto poll=[&]{return paging.requestChunk(request,{},1);};
  paging.cache.values[key]=&original;
  jobs.next={false,false,&original};assert(!poll().mReady && !completed.contains(&original));
  jobs.next={true,false,&original};assert(poll().mReady && completed.contains(&original));
  // Rebuilt wrappers after retired queue records reuse the current completed node.
  jobs.clear();int calls=jobs.calls;auto ready=poll();
  assert(ready.mReady && ready.mNode.get()==&original && jobs.calls==calls);
  ++paging.mSelectionRevision;assert(paging.updateAsyncState(2));
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
 std::cout<<"PASS: production traversal keeps terrain, separates ray/camera readiness, preserves active/compatibility paths and aborts; publication rejects clear/model/VFS changes during assembly\n";
 std::cout<<"PASS: production requests reuse completed current nodes without new jobs; pending/replaced nodes and all graphics generations retain preparation gates\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'streaming.cpp';cpp.write_text(source)
    exe=Path(tmp)/('streaming.exe' if os.name=='nt' else 'streaming')
    if os.name=='nt':
        command=['cl','/nologo','/std:c++20','/EHsc',str(cpp),'/Fe:'+str(exe),'/Fo:'+str(Path(tmp)/'streaming.obj')]
    else:
        command=['c++','-std=c++20','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)]
    subprocess.run(command,check=True)
    subprocess.run([str(exe)],check=True)
