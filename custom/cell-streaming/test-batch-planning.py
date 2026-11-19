"""Execute the actual Scene request bodies with counted resource/world boundaries.
This checks scheduling and timestamp refresh, not binary model decoding or gameplay.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'apps/openmw/mwworld/scene.cpp').read_text()
def extract(signature):
    start = source.index(signature)
    begin = source.index('{', start)
    end, depth = begin + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include <components/sceneutil/preloadplancache.hpp>
#include <components/vfs/pathutil.hpp>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <tuple>
#include <vector>
namespace Diagnostics { struct Scope { template<class... T> Scope(T&&...) {} }; }
namespace Misc { bool enabled=true; bool usePreloadPlanCache(){return enabled;} }
namespace osg { template<class T> struct ref_ptr : std::shared_ptr<T> {
 using std::shared_ptr<T>::shared_ptr; ref_ptr(T* p):std::shared_ptr<T>(p){};
 template<class U>ref_ptr(const ref_ptr<U>& p):std::shared_ptr<T>(p){} }; }
namespace SceneUtil {
 struct WorkItem { bool done=false; virtual ~WorkItem()=default; bool isDone()const{return done;} };
 template<class Key> struct PendingPreload {
  using Generation=std::pair<std::uint64_t,std::uint64_t>;
  struct Entry{Generation generation;osg::ref_ptr<WorkItem> item;};std::map<Key,Entry> items;
  bool contains(const Key& key,Generation generation)const{auto i=items.find(key);return i!=items.end()&&i->second.generation==generation&&!i->second.item->isDone();}
  void insert(const Key& key,Generation generation,osg::ref_ptr<WorkItem> item){items.insert_or_assign(key,Entry{generation,item});}
 };
 struct Queue {std::vector<osg::ref_ptr<WorkItem>> jobs;void addWorkItem(osg::ref_ptr<WorkItem> item){jobs.push_back(item);}};
}
namespace VFS { struct Manager{std::uint64_t revision=1;std::uint64_t getIndexRevision()const{return revision;}}; }
namespace Resource {
 struct SceneManager{VFS::Manager vfs;std::uint64_t revision=1;bool loaded=false;
 bool checkLoaded(VFS::Path::NormalizedView,double){return loaded;}
 std::uint64_t modelSettingsRevision()const{return revision;}const VFS::Manager* getVFS()const{return &vfs;}};
 struct System {SceneManager scene;SceneManager* getSceneManager(){return &scene;}const VFS::Manager* getVFS(){return &scene.vfs;}};
}
namespace Misc::ResourceHelpers {VFS::Path::Normalized correctActorModelPath(VFS::Path::NormalizedView p,const VFS::Manager*){return VFS::Path::Normalized(p);}}
namespace ESM {using RefId=int;struct ExteriorCellLocation{int mX,mY;RefId mWorldspace=0;auto operator<=>(const ExteriorCellLocation&)const=default;};}
namespace MWWorld {
 struct CellStore {ESM::ExteriorCellLocation location;bool exterior=true;bool isExterior()const{return exterior;}const CellStore* getCell()const{return this;}int getGridX()const{return location.mX;}int getGridY()const{return location.mY;}int getWorldSpace()const{return location.mWorldspace;}};
 struct WorldModel{std::map<ESM::ExteriorCellLocation,CellStore> cells;int lookups=0;CellStore& getExterior(ESM::ExteriorCellLocation key){++lookups;return cells.try_emplace(key,CellStore{key}).first->second;}};
 struct World{WorldModel model;WorldModel& getWorldModel(){return model;}};
 struct Preloader{std::size_t capacity=4;std::vector<std::pair<CellStore*,double>> requests;std::size_t getMaxCacheSize()const{return capacity;}void preload(CellStore& cell,double time){requests.emplace_back(&cell,time);}};
 struct Rendering{Resource::System resources;SceneUtil::Queue queue;double time=1;Resource::System* getResourceSystem(){return &resources;}SceneUtil::Queue* getWorkQueue(){return &queue;}double getReferenceTime()const{return time;}};
 struct PreloadMeshItem:SceneUtil::WorkItem{PreloadMeshItem(VFS::Path::NormalizedView,Resource::SceneManager*){}};
 template<class F>void iterateOverCellsAround(int x,int y,int radius,F f){for(int i=x-radius;i<=x+radius;++i)for(int j=y-radius;j<=y+radius;++j)f(i,j);}
 void sortCellsToLoad(int x,int y,std::vector<std::pair<int,int>>& cells){std::stable_sort(cells.begin(),cells.end(),[&](auto a,auto b){auto dist=[&](auto v){return (v.first-x)*(v.first-x)+(v.second-y)*(v.second-y);};return dist(a)<dist(b);});}
 struct Scene{
 Rendering mRendering;World mWorld;std::unique_ptr<Preloader> mPreloader=std::make_unique<Preloader>();int mHalfGridSize=1;
 std::vector<osg::ref_ptr<SceneUtil::WorkItem>> mWorkItems;
 SceneUtil::PendingPreload<VFS::Path::Normalized> mPendingMeshes;std::size_t mMeshRequestsCoalesced=0;
 using GridPlanKey=std::tuple<ESM::ExteriorCellLocation,int,std::size_t>;
 SceneUtil::PreloadPlanCache<GridPlanKey,int,std::vector<CellStore*>> mDestinationPlans{32};
 void preload(const std::string&,bool=false);void preloadCellWithSurroundings(CellStore&);
 };
'''
code += extract('void Scene::preload(const std::string& mesh, bool useAnim)')
code += extract('void Scene::preloadCellWithSurroundings(CellStore& cell)')
code += r'''
}
int main(){using namespace MWWorld;
 Scene meshes;
 meshes.preload("MESHES\\A.NIF");meshes.preload("meshes/a.nif");meshes.preload("meshes/a.nif",true);
 assert(meshes.mRendering.queue.jobs.size()==1&&meshes.mMeshRequestsCoalesced==2);
 meshes.mRendering.queue.jobs.back()->done=true;meshes.preload("meshes/a.nif");assert(meshes.mRendering.queue.jobs.size()==2);
 ++meshes.mRendering.resources.scene.revision;meshes.preload("meshes/a.nif");assert(meshes.mRendering.queue.jobs.size()==3);
 ++meshes.mRendering.resources.scene.vfs.revision;meshes.preload("meshes/a.nif");assert(meshes.mRendering.queue.jobs.size()==4);
 meshes.mRendering.resources.scene.loaded=true;meshes.preload("meshes/a.nif");assert(meshes.mRendering.queue.jobs.size()==4);
 Scene plans;CellStore target{{5,6,0}};plans.preloadCellWithSurroundings(target);
 assert(plans.mPreloader->requests.size()==4&&plans.mWorld.model.lookups==4);
 auto first=plans.mPreloader->requests;plans.mRendering.time=2;plans.preloadCellWithSurroundings(target);
 assert(plans.mPreloader->requests.size()==8&&plans.mWorld.model.lookups==4);
 for(int i=0;i<4;++i){assert(plans.mPreloader->requests[i+4].first==first[i].first);assert(plans.mPreloader->requests[i+4].second==2);}
 plans.mPreloader->capacity=6;plans.preloadCellWithSurroundings(target);assert(plans.mWorld.model.lookups==10);
 target.location.mWorldspace=7;plans.preloadCellWithSurroundings(target);assert(plans.mWorld.model.lookups==16);
 plans.mDestinationPlans.clear();plans.preloadCellWithSurroundings(target);assert(plans.mWorld.model.lookups==22);
 plans.mPreloader->capacity=0;plans.preloadCellWithSurroundings(target);assert(plans.mWorld.model.lookups==22);
 Misc::enabled=false;Scene previous;previous.preload("meshes/a.nif");previous.preload("meshes/a.nif");assert(previous.mRendering.queue.jobs.size()==2);
 std::cout<<"PASS: pending normalized requests, failure retry, generation changes, full-cache refresh, capacity/worldspace/reset invalidation\n";
}
'''
with tempfile.TemporaryDirectory() as temp:
    cpp=Path(temp)/'planning.cpp';exe=Path(temp)/'planning';cpp.write_text(code)
    subprocess.run(['g++','-std=c++20','-O2','-I',str(root),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
Path('batch-planning-tests.json').write_text(json.dumps({'result':'PASS','scene_sha256':hashlib.sha256(source.encode()).hexdigest(),
 'scope':'actual Scene request bodies with counted external boundaries; native helpers are tested separately','in_game_tested':False},indent=2)+'\n')
