"""Exercise the production preload grouping body with counted scene boundaries.

Real vertex/cache/pool and GL tests run separately. This test checks the actual
ordering and cancellation decisions without simulating a graphics driver.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'components/terrain/quadtreeworld.cpp').read_text()
start = source.index('        const auto build = [&](std::size_t first, std::size_t last)')
end = source.index('        if (separateScenery && !abort)', start)
body = source[start:end]

preamble = r'''
#include <components/misc/combinedperformance.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>
namespace Diagnostics::Loading {enum class Stage {RenderNodes};struct Timer{explicit Timer(Stage){}};}
namespace Debug {enum {Info};}
struct Log {explicit Log(int){}template<class T>Log& operator<<(const T&){return *this;}};
namespace Settings {struct General{bool mNativeJobsEnabled=true;};General value;General& general(){return value;}}
struct Visitor {unsigned promote(int){return 0;}};
struct Node {int id;void accept(Visitor&) {}};
struct ViewDataEntry {Node* mNode=nullptr;unsigned mLodFlags=0;std::shared_ptr<Node> mRenderingNode;bool mRenderingComplete=false;};
struct ViewData {std::vector<ViewDataEntry> entries;auto& getEntry(unsigned i){return entries.at(i);}int getViewPoint(){return 0;}};
struct ChunkRequest {int id;bool mActiveGrid;};
struct Pool {unsigned workers;unsigned workerCount(){return workers;}};
struct Ground {
 std::vector<unsigned> groups;std::atomic_bool* cancel=nullptr;
 void prepareVertices(std::span<const ChunkRequest> chunks,Pool&,const std::atomic_bool&){
  assert(chunks.size()<=4);groups.push_back(chunks.size());if(cancel)*cancel=true;
 }
};
struct BatchChunkManager {
 using Fetch=std::function<int()>;
 bool enabled=true;unsigned calls=0;bool canPreloadBatches(){return enabled;}
 template<class F>void preloadChunks(std::span<const ChunkRequest> chunks,int,const std::atomic_bool& abort,F load){
  ++calls;for(unsigned i=0;i<chunks.size()&&!abort;++i)load(i,[] {return 1;});
 }
};
struct Arrival {
 struct Entry {struct Key {Node* node;unsigned flags;};Key key;std::shared_ptr<Node> node;std::size_t bytes;};
 std::vector<int> visible;
 void publish(const std::vector<Entry>& entries){for(const auto& e:entries)visible.push_back(e.node->id);}
};
using ArrivalView=Arrival;
struct Footprint {std::size_t measure(Node&){return 1;}};
struct Result {std::vector<int> loads,visible;std::vector<unsigned> groups;unsigned progress=0,nearCalls=0;bool aborted=false;};
Result exercise(unsigned workers,bool modelBatch,bool cancel,bool fail,bool cached,bool nativeEnabled=true)
{
 Settings::value.mNativeJobsEnabled=nativeEnabled;
 Pool pool{workers};auto* mTerrainJobPool=&pool;
 Ground ground;auto* mChunkManager=&ground;
 BatchChunkManager models;models.enabled=modelBatch;auto* mBatchChunkManager=&models;
 std::atomic_bool abort=false;if(cancel)ground.cancel=&abort;
 std::array<Node,8> nodes;ViewData data;auto* vd=&data;
 for(int i=0;i<8;++i){nodes[i].id=i;data.entries.push_back({&nodes[i]});}
 if(cached){data.entries[1].mRenderingNode=std::make_shared<Node>(nodes[1]);data.entries[1].mRenderingComplete=true;}
 std::vector<unsigned> ordered(8);std::iota(ordered.begin(),ordered.end(),0);
 const std::size_t near=3;const int grid=0;const float cellWorldSize=8192;const bool separateScenery=false;
 auto arrival=std::make_shared<Arrival>();Footprint footprint;Visitor nearCompile;
 int compileValue=0;auto* compile=&compileValue;std::size_t nearCompileSets=0;
 Result result;
 const auto advance=[&]{++result.progress;};
 const auto updateRenderingLod=[](ViewDataEntry&,ViewData*){};
 const auto getChunkRequest=[](const ViewDataEntry& e,int){return ChunkRequest{e.mNode->id,e.mNode->id<3};};
 const auto loadRenderingNode=[&](ViewDataEntry& e,ViewData*,float,const ChunkRequest& request,
  bool,const std::atomic_bool*,const BatchChunkManager::Fetch*,bool,int,bool){
  if(fail && request.id==4)throw std::runtime_error("injected node failure");
  result.loads.push_back(request.id);e.mRenderingNode=std::make_shared<Node>(*e.mNode);e.mRenderingComplete=true;
 };
 const auto nearReady=[&](std::size_t n,std::size_t far){
  ++result.nearCalls;assert(n==3 && far==5 && result.progress==3);
  assert(arrival->visible==std::vector<int>({0,1,2}));
 };
 try {
'''
postamble = r'''
 }catch(const std::runtime_error&){assert(fail);}
 result.visible=arrival->visible;result.groups=ground.groups;result.aborted=abort;return result;
}
int main(){
 const bool optimized=Misc::useTerrainPreparation() && Misc::useNativeJobs();
 for(unsigned workers:{0u,3u})for(bool modelBatch:{false,true})for(bool cached:{false,true}){
  const auto result=exercise(workers,modelBatch,false,false,cached);
  assert(result.visible==std::vector<int>({0,1,2,3,4,5,6,7}));
  auto expected=std::vector<int>({0,1,2,3,4,5,6,7});if(cached)expected.erase(expected.begin()+1);
  assert(result.loads==expected && result.progress==8 && result.nearCalls==1);
  if(optimized && workers)assert(result.groups==std::vector<unsigned>({cached?2u:3u,4u}));
  else assert(result.groups.empty());
 }
 const auto cancelled=exercise(3,true,true,false,false);
 if(optimized)assert(cancelled.aborted && cancelled.loads.empty() && cancelled.visible.empty() && cancelled.nearCalls==0);
 const auto failed=exercise(3,true,false,true,false);
 assert(failed.loads==std::vector<int>({0,1,2,3}));
 assert(failed.visible==std::vector<int>({0,1,2}) && failed.nearCalls==1 && failed.progress==4);
 const auto settingsOff=exercise(3,false,false,false,false,false);
 assert(settingsOff.groups.empty() && settingsOff.progress==8 && settingsOff.visible.size()==8);
 std::cout<<"PASS: production terrain grouping keeps bounded near/far ordering, owner publication, progress, fallback, cancellation and failure semantics\n";
}
'''

with tempfile.TemporaryDirectory(prefix='terrain-planning-') as temp:
    path = Path(temp)
    cpp = path / 'planning.cpp'
    cpp.write_text(preamble + body + postamble)
    exe = path / ('planning.exe' if os.name == 'nt' else 'planning')
    command = (['cl', '/nologo', '/std:c++20', '/EHsc', '/O2', '/UNDEBUG', '/I'+str(root), str(cpp), '/Fe:'+str(exe)]
               if os.name == 'nt' else ['g++','-std=c++20','-O2','-UNDEBUG','-I'+str(root),str(cpp),'-o',str(exe)])
    subprocess.run(command, check=True, cwd=path)
    cases=[]
    for name, changes in [('enabled', {'OPENMW_TERRAIN_PREPARATION':'1','OPENMW_COMBINED_PERFORMANCE':'1'}),
                          ('terrain-off', {'OPENMW_TERRAIN_PREPARATION':'0','OPENMW_COMBINED_PERFORMANCE':'1'}),
                          ('master-off', {'OPENMW_TERRAIN_PREPARATION':'1','OPENMW_COMBINED_PERFORMANCE':'0'})]:
        env=dict(os.environ,OPENMW_NATIVE_JOBS='1',**changes)
        subprocess.run([str(exe)], env=env, check=True)
        cases.append(name)
    (root/'terrain-planning-tests.json').write_text(json.dumps({
        'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'modes_passed':cases,
        'scope':'Production grouping body with counted scene boundaries; native geometry and GL tested separately.'},indent=2)+'\n')
