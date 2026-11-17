"""Execute the production scenery controller with real OSG ownership/cache expiry.
The resource loader and completion gates are controlled; this does not measure game timing.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--include-dir')
parser.add_argument('--lib-dir')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'apps/openmw/mwrender/objectpaging.cpp').read_text()

def extract(signature):
    start = source.index(signature)
    begin = source.index('{', start)
    end, depth = begin + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include <components/terrain/scenerycoverage.hpp>
#include <components/terrain/arrivalview.hpp>
#include <components/sceneutil/asyncpreparation.hpp>
#include <components/sceneutil/compilepriority.hpp>
#include <components/sceneutil/scenefootprint.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/resource/objectcache.hpp>
#include <osg/GraphicsContext>
#include <osg/FrameStamp>
#include <osg/Group>
#include <cassert>
#include <chrono>
#include <iostream>
#include <optional>
#include <thread>
namespace Debug { enum {Info}; }
struct Log {explicit Log(int){}template<class T>Log& operator<<(const T&){return *this;}};
namespace Diagnostics::Loading {enum class Stage {SceneryRequired};struct Timer{explicit Timer(Stage){}};}
int getCellSize(int){return 8192;}
struct Shader {unsigned revision=0;unsigned compileRevision()const{return revision;}};
struct Scene {Shader shader;Shader& getShaderManager(){return shader;}};
using ChunkId=std::tuple<osg::Vec2f,float,bool>;
struct ObjectPaging {
 struct ChunkRequest {float mSize;osg::Vec2f mCenter;unsigned mLodFlags=0;bool mActiveGrid=false;};
 using CacheEpoch=std::tuple<unsigned,unsigned,unsigned>;
 struct PendingCompile;struct SceneryBridge;struct SceneryState;struct ScenerySnapshot;struct AsyncState;
 std::unique_ptr<SceneryState> mScenery;std::unique_ptr<AsyncState> mAsync;
 std::mutex mRefTrackerMutex;std::atomic_uint64_t mSelectionRevision{0};int mWorldspace=0;
 Scene scene;Scene* mSceneManager=&scene;CacheEpoch epoch{};bool asynchronous=true;
 osg::ref_ptr<Resource::GenericObjectCache<ChunkId>> cache=new Resource::GenericObjectCache<ChunkId>;
 osg::ref_ptr<SceneUtil::WorkQueue> queue=new SceneUtil::WorkQueue(1);
 std::map<ChunkId,std::shared_ptr<std::atomic_bool>> gates;
 unsigned bridgeBuilds=0;
 ObjectPaging();~ObjectPaging();
 CacheEpoch cacheEpoch()const{return epoch;}
 std::uint64_t selectionRevision(const ChunkId&)const{return mSelectionRevision.load();}
 bool canPrepareAsync()const{return asynchronous;}
 osg::ref_ptr<osg::Node> getCachedChunk(const ChunkId& key){return static_cast<osg::Node*>(cache->getRefFromObjectCache(key).get());}
 osg::ref_ptr<osg::Node> fetchChunk(const ChunkRequest& r,const osg::Vec3f&,bool,const std::atomic_bool*,const void*,bool){
  ChunkId key{r.mCenter,r.mSize,false};if(auto n=getCachedChunk(key))return n;
  osg::ref_ptr<osg::Group> n=new osg::Group;n->setName("full");cache->addEntryToObjectCache(key,n);return n;
 }
 osg::ref_ptr<SceneryBridge> getSceneryBridge(Terrain::SceneryCell,const std::atomic_bool*,bool);
 void requestChunk(const ChunkRequest&,const osg::Vec3f&,double,bool);
 void acceptScenery(std::span<const ChunkRequest>,const osg::Vec3f&,const osg::Vec4i&,osg::NodeVisitor&,double,
  std::shared_ptr<Terrain::SceneryView>&,const std::shared_ptr<Terrain::SceneryView>&);
 void drain();
};
''' + '\n'.join(extract(s) + ';' for s in [
    '    struct ObjectPaging::PendingCompile', '    struct ObjectPaging::SceneryBridge',
    '    struct ObjectPaging::SceneryState', '    struct ObjectPaging::ScenerySnapshot',
    '    struct ObjectPaging::AsyncState']) + r'''
ObjectPaging::ObjectPaging(){mScenery=std::make_unique<SceneryState>();mAsync=std::make_unique<AsyncState>(queue);}
ObjectPaging::~ObjectPaging(){mAsync.reset();}
osg::ref_ptr<ObjectPaging::SceneryBridge> ObjectPaging::getSceneryBridge(Terrain::SceneryCell cell,const std::atomic_bool*,bool required){
 auto old=mScenery->mBridges.find(cell);if(old!=mScenery->mBridges.end())return old->second.mNode;
 ++bridgeBuilds;osg::ref_ptr<SceneryBridge> b=new SceneryBridge;b->mNode=new osg::Group;
 b->mNode->setName("bridge:"+std::to_string(cell.first)+","+std::to_string(cell.second));b->mBytes=1024;
 mScenery->mBridges.emplace(cell,SceneryState::Entry{b,b->mBytes,++mScenery->mUse,required?mScenery->mFrame:0});
 mScenery->mBytes+=b->mBytes;return b;
}
void ObjectPaging::requestChunk(const ChunkRequest& r,const osg::Vec3f& point,double time,bool){
 ChunkId key{r.mCenter,r.mSize,false};auto& gate=gates[key];if(!gate)gate=std::make_shared<std::atomic_bool>(false);
 mAsync->mRequests.request({key,selectionRevision(key)},time,[this,r,point,gate](const std::atomic_bool&){
  return AsyncState::Requests::Prepared{fetchChunk(r,point,false,nullptr,nullptr,false),gate,{},1024};
 });
}
struct Barrier:SceneUtil::WorkItem {void doWork()override{}};
void ObjectPaging::drain(){mAsync->mRequests.update(1);osg::ref_ptr<Barrier> b=new Barrier;queue->addWorkItem(b);b->waitTillDone();}
''' + extract('    void ObjectPaging::acceptScenery(') + r'''
struct Seen:osg::NodeVisitor {
 int full=0,bridges=0;std::set<std::string> cells;
 Seen():NodeVisitor(TRAVERSE_ALL_CHILDREN){setFrameStamp(new osg::FrameStamp);}
 void apply(osg::Group& n)override{if(n.getName()=="full")++full;
  if(n.getName().starts_with("bridge:")){++bridges;cells.insert(n.getName());}traverse(n);}
 void reset(unsigned frame){full=bridges=0;cells.clear();osg::ref_ptr<osg::FrameStamp> stamp=new osg::FrameStamp;
  stamp->setFrameNumber(frame);setFrameStamp(stamp);}
};
template<class F>void until(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 while(!f()){assert(std::chrono::steady_clock::now()<end);std::this_thread::yield();}}
int main(){
 using R=ObjectPaging::ChunkRequest;
 const std::array requests{R{.5f,{.25f,.25f}},R{.5f,{.75f,.25f}},R{.5f,{.25f,.75f}},R{.5f,{.75f,.75f}}};
 const auto key=[&](int i){return ChunkId{requests[i].mCenter,requests[i].mSize,false};};
 const auto order=Terrain::sceneryCoverage(std::span<const R>(requests),osg::Vec4i{}).front().mRequests;
 ObjectPaging pager;Seen seen;std::shared_ptr<Terrain::SceneryView> current;
 const auto draw=[&](unsigned frame,const std::shared_ptr<Terrain::SceneryView>& incoming={}){
  seen.reset(frame);pager.acceptScenery(requests,{},{},seen,frame*.1,current,incoming);};
 draw(1);assert(seen.bridges==1&&seen.full==0&&pager.bridgeBuilds==1);
 until([&]{return pager.getCachedChunk(key(order[0]))&&pager.getCachedChunk(key(order[1]));});
 *pager.gates[key(order[0])]=true;*pager.gates[key(order[1])]=true;
 draw(2);assert(seen.bridges==1&&seen.full==0);
 assert(std::dynamic_pointer_cast<ObjectPaging::ScenerySnapshot>(current)->mBatches.size()==2);
 until([&]{return pager.getCachedChunk(key(order[2]))&&pager.getCachedChunk(key(order[3]));});
 pager.drain();pager.cache->update(100,0.01);pager.cache->update(1000,0.01);
 assert(pager.getCachedChunk(key(order[0]))&&pager.getCachedChunk(key(order[1])));
 *pager.gates[key(order[2])]=true;draw(3);assert(seen.bridges==1&&seen.full==0);
 assert(std::dynamic_pointer_cast<ObjectPaging::ScenerySnapshot>(current)->mBatches.size()==3);
 *pager.gates[key(order[3])]=true;draw(4);assert(seen.bridges==0&&seen.full==4);
 pager.drain();pager.mAsync->mCompleted.clear();
 auto incoming=current;current.reset();pager.cache->update(2000,0.01);
 const auto submitted=pager.mAsync->mRequests.submitted();draw(5,incoming);
 assert(seen.bridges==0&&seen.full==4&&pager.bridgeBuilds==1);
 assert(pager.mAsync->mRequests.submitted()==submitted);
 auto previousPlan=std::dynamic_pointer_cast<ObjectPaging::ScenerySnapshot>(current)->mPlan;
 draw(6);assert(std::dynamic_pointer_cast<ObjectPaging::ScenerySnapshot>(current)->mPlan==previousPlan);
 ++pager.mScenery->mCompileGeneration;pager.mAsync->mCompleted.clear();pager.mAsync->mRequests.clear();
 for(auto& [k,g]:pager.gates)*g=false;
 draw(7,incoming);assert(seen.bridges==1&&seen.full==0);
 ++pager.mSelectionRevision;pager.cache->clear();pager.mScenery->clear();pager.mAsync->mRequests.clear();
 draw(8,incoming);assert(seen.bridges==1&&seen.full==0&&pager.bridgeBuilds==2);
 // Arrival cover remains owned even when the speculative cache evicts it.
 ObjectPaging arrival;auto retained=std::make_shared<ObjectPaging::ScenerySnapshot>();
 retained->mEpoch=arrival.cacheEpoch();retained->mSelection=0;retained->mShader=0;
 retained->mBridges[{0,0}]=arrival.getSceneryBridge({0,0},nullptr,false);arrival.mScenery->clear();
 std::shared_ptr<Terrain::SceneryView> landed;seen.reset(9);
 arrival.acceptScenery(requests,{},{},seen,.9,landed,retained);
 assert(seen.bridges==1&&arrival.bridgeBuilds==1);
 // Newly active ownership never receives retained distant geometry.
 ObjectPaging migration;const std::array large{R{2,{1,1}}};std::shared_ptr<Terrain::SceneryView> shifted;
 seen.reset(10);migration.acceptScenery(large,{},osg::Vec4i(0,0,1,2),seen,1,shifted,{});
 assert(seen.full==0&&seen.bridges==2);
 assert(!seen.cells.contains("bridge:0,0")&&!seen.cells.contains("bridge:0,1"));
 std::cout<<"PASS: production scenery handover, partial-candidate expiry, incoming optimized receipts, arrival ownership, context/selection invalidation, plan reuse and active-grid exclusion.\n";
}
'''

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'handover.cpp'
    exe = Path(directory) / 'handover'
    path.write_text(code)
    command = ['g++', '-std=c++20', '-O1', '-g', '-ffunction-sections', '-fdata-sections', '-pthread', '-I', str(root)]
    if args.include_dir:
        command += ['-I', args.include_dir]
    command += [str(path), 'components/sceneutil/workqueue.cpp', 'components/sceneutil/unrefqueue.cpp',
                'components/sceneutil/compilepriority.cpp', 'components/sceneutil/positionattitudetransform.cpp',
                'components/debug/debuglog.cpp', '-Wl,--gc-sections']
    if args.lib_dir:
        command += ['-L', args.lib_dir]
    command += ['-losgUtil', '-losg', '-lOpenThreads', '-o', str(exe)]
    subprocess.run(command, cwd=root, check=True)
    env = dict(os.environ)
    if args.lib_dir:
        env['LD_LIBRARY_PATH'] = args.lib_dir
    subprocess.run([str(exe)], cwd=root, env=env, check=True)
    (root / 'scenery-handover-tests.json').write_text(json.dumps({
        'result': 'PASS', 'production_sha256': hashlib.sha256(source.encode()).hexdigest(),
        'fixture_sha256': hashlib.sha256(code.encode()).hexdigest(),
        'real_osg_ownership_and_cache': True, 'real_graphics_context': False, 'in_game_measured': False
    }, indent=2) + '\n')
