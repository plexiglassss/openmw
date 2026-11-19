"""Execute the production static-reader/scheduling body with counted dependencies.
Binary ESM parsing and worker synchronization are not emulated by this fixture.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'apps/openmw/mwworld/cellpreloader.cpp').read_text()
def extract(signature):
    start = source.index(signature)
    begin = source.index('{', start)
    end, depth = begin + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include <components/vfs/pathutil.hpp>
#include <components/sceneutil/preparationcost.hpp>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <vector>
namespace osg {struct Referenced{virtual ~Referenced()=default;};struct Node:Referenced{};
template<class T>using ref_ptr=std::shared_ptr<T>;}
namespace Debug{enum{Warning};}std::ostream& Log(int){return std::cerr;}
int reads=0,cancelAt=0,worldCalls=0;const std::atomic_bool* runningAbort=nullptr;
namespace ESM {
using RefNum=int;using RefId=int;
enum{REC_STAT=1,REC_ACTI,REC_DOOR,REC_CONT,REC_LIGH};
struct Path{VFS::Path::Normalized value;VFS::Path::NormalizedView getNormalized()const{return value;}};
struct Static{Path mModel;};struct Activator{Path mModel;};struct Door{Path mModel;};
struct Container{Path mModel;};struct Light{Path mModel;};
struct Entry{int number,id;bool deleted=false,moved=false;float scale=1;};
struct ESMReader{const std::vector<Entry>* entries=nullptr;std::size_t offset=0;};
struct CellRef{int mRefNum=0,mRefID=0;float mScale=1;};struct MovedCellRef{};
struct Cell{
 static constexpr RefId sDefaultWorldspaceId=0;
 struct Context{int index;};std::vector<Context> mContextList;
 std::vector<std::vector<Entry>> records;std::vector<RefNum> mMovedRefs;
 std::vector<std::pair<CellRef,bool>> mLeasedRefs;
 enum class GetNextRefMode{LoadOnlyNotMoved};
 void restore(ESMReader& reader,std::size_t i)const{reader={&records[i],0};}
 static bool getNextRef(ESMReader& reader,CellRef& ref,bool& deleted,MovedCellRef&,bool& moved,GetNextRefMode){
  if(reader.offset==reader.entries->size())return false;
  const auto e=reader.entries->at(reader.offset++);ref={e.number,e.id,e.scale};deleted=e.deleted;moved=e.moved;
  if(++reads==cancelAt)const_cast<std::atomic_bool*>(runningAbort)->store(true);return true;
 }
};
struct ExteriorCellLocation{int mX,mY;RefId mWorldspace=0;
 auto operator<=>(const ExteriorCellLocation&)const=default;};
struct ReadersCache{struct Busy{mutable ESMReader reader;ESMReader& operator*()const{return reader;}};
 Busy get(std::size_t){return {};}};
}
namespace Misc {bool useCostAwarePreload(){return true;}}
namespace Misc::ResourceHelpers{VFS::Path::Normalized correctMeshPath(VFS::Path::NormalizedView p){return VFS::Path::NormalizedView("meshes")/p;}}
namespace Resource {struct SceneInstanceCache{using TicketPtr=std::shared_ptr<int>;};}
namespace SceneUtil {
struct WorkQueue{};struct SceneFootprint{std::size_t measure(osg::Node&){return 512;}};
template<class Key,class Value>struct AsyncPreparation{
 struct Prepared{Value mValue{};std::shared_ptr<std::atomic_bool> mCompletion;std::function<void()> mCancel;std::size_t mEstimatedBytes=0;};
 struct Entry{std::function<Prepared(const std::atomic_bool&)> factory;Prepared value;double last=0;};
 std::map<Key,Entry> entries;int submissions=0,clears=0;
 template<class... A>explicit AsyncPreparation(std::shared_ptr<WorkQueue>,A...){}
 void clear(){entries.clear();++clears;}bool contains(Key k)const{return entries.contains(k);}std::size_t size()const{return entries.size();}
 void poll(Key k,double t){entries.at(k).last=t;}
 template<class F>void request(Key k,double t,F f){entries.emplace(k,Entry{std::move(f),{},t});++submissions;}
 void run(Key k){std::atomic_bool abort{false};runningAbort=&abort;entries.at(k).value=entries.at(k).factory(abort);runningAbort=nullptr;}
};
}
template<class T>struct Store{std::map<int,T> values;const T* searchStatic(int id)const{auto p=values.find(id);return p==values.end()?nullptr:&p->second;}};
template<>struct Store<ESM::Cell>{std::map<std::pair<int,int>,ESM::Cell> values;
 const ESM::Cell* searchStatic(int x,int y)const{auto p=values.find({x,y});return p==values.end()?nullptr:&p->second;}};
struct ESMStore{
 std::tuple<Store<ESM::Cell>,Store<ESM::Static>,Store<ESM::Activator>,Store<ESM::Door>,Store<ESM::Container>,Store<ESM::Light>> stores;
 template<class T>auto& get(){return std::get<Store<T>>(stores);}template<class T>const auto& get()const{return std::get<Store<T>>(stores);}
 int findStatic(int id)const{if(get<ESM::Static>().searchStatic(id))return ESM::REC_STAT;
  if(get<ESM::Activator>().searchStatic(id))return ESM::REC_ACTI;if(get<ESM::Door>().searchStatic(id))return ESM::REC_DOOR;
  if(get<ESM::Container>().searchStatic(id))return ESM::REC_CONT;if(get<ESM::Light>().searchStatic(id))return ESM::REC_LIGH;return 0;}
};
namespace MWBase{struct Environment{ESMStore* store=nullptr;static Environment& get(){static Environment e;return e;}
 ESMStore* getESMStore(){return store;}void getWorldModel(){++worldCalls;throw 1;}};}
struct VFSState{std::uint64_t revision=0;auto getIndexRevision()const{return revision;}};
struct Scene{VFSState vfs;std::uint64_t revision=0;std::set<std::string> loaded;int clones=0;
 auto modelSettingsRevision()const{return revision;}VFSState* getVFS(){return &vfs;}
 bool getCachedTemplate(VFS::Path::NormalizedView path){return loaded.contains(std::string(path.value()));}
 auto getTemplate(VFS::Path::NormalizedView path){loaded.insert(std::string(path.value()));return std::make_shared<osg::Node>();}
 auto cacheInstance(VFS::Path::NormalizedView,const std::atomic_bool&){++clones;return std::make_shared<int>(1);}
};
struct Shape{};
struct Shapes{int shapes=0,instances=0;std::multiset<std::pair<std::string,float>> scales;
 auto getShape(VFS::Path::NormalizedView){++shapes;return std::make_shared<Shape>();}
 auto cacheInstance(VFS::Path::NormalizedView path,float scale){++instances;scales.emplace(path.value(),scale);return std::make_shared<Shape>();}};
struct Resources{Scene* scene;Scene* getSceneManager(){return scene;}};
struct ExistingCell{ESM::ExteriorCellLocation location;bool isExterior()const{return true;}const ExistingCell* getCell()const{return this;}
 ESM::ExteriorCellLocation getExteriorCellLocation()const{return location;}};
struct CellPreloader{
 struct StaticPreloads;std::unique_ptr<StaticPreloads> mStaticPreloads;
 std::shared_ptr<SceneUtil::WorkQueue> mWorkQueue=std::make_shared<SceneUtil::WorkQueue>();std::size_t mMaxCacheSize=9;
 std::map<const ExistingCell*,int> mPreloadCells;Resources* mResourceSystem;Shapes* mBulletShapeManager;
 bool mPreloadInstances=true;
 void preloadStaticExterior(ESM::ExteriorCellLocation,double);
};
''' + extract('    struct CellPreloader::StaticPreloads') + ';\n' + extract('    void CellPreloader::preloadStaticExterior(') + r'''
int main(){
 ESMStore store;MWBase::Environment::get().store=&store;Scene scene;Shapes shapes;Resources resources{&scene};
 CellPreloader preload;preload.mResourceSystem=&resources;preload.mBulletShapeManager=&shapes;
 const ESM::ExteriorCellLocation target{1,2,0};
 auto& cell=store.get<ESM::Cell>().values[{1,2}];cell.mContextList={{0},{1}};
 cell.records={{{1,1},{2,2},{3,3},{4,4}},{{1,1,true},{2,5,false,false,1.5f},{6,6,false,true}}};
 cell.mMovedRefs={3};cell.mLeasedRefs={{{7,7,0.75f},false},{{4,4},true}};
 for(int i=1;i<=7;++i)store.get<ESM::Door>().values[i].mModel.value=VFS::Path::Normalized(std::to_string(i)+".nif");
 preload.preloadStaticExterior(target,1);assert(preload.mStaticPreloads->mJobs.size()==1);
 // Changing the source cell after admission cannot change its owned snapshot.
 cell.records.clear();preload.mStaticPreloads->mJobs.run(target);
 assert(scene.loaded==(std::set<std::string>{"meshes/5.nif","meshes/7.nif"}));
 assert(shapes.shapes==2&&shapes.instances==2&&scene.clones==2&&worldCalls==0);
 assert((shapes.scales==std::multiset<std::pair<std::string,float>>{{"meshes/5.nif",1.5f},{"meshes/7.nif",0.75f}}));
 assert(preload.mStaticPreloads->mSamples.at(target)->mComplete.load());
 assert(preload.mStaticPreloads->mSamples.at(target)->mColdAssets==2);
 preload.mStaticPreloads->observe();
 assert(preload.mStaticPreloads->mCost.samples()==1 && preload.mStaticPreloads->mSamples.empty());
 preload.preloadStaticExterior(target,2);assert(preload.mStaticPreloads->mJobs.submissions==1);
 assert(preload.mStaticPreloads->mJobs.entries.at(target).last==2);
 // Bounded unique models and collision/render instances, even with thousands of refs.
 cell.mContextList={{0}};cell.records={{}};cell.mMovedRefs.clear();cell.mLeasedRefs.clear();
 for(int i=1;i<=2000;++i){const int id=10+i%200;cell.records[0].push_back({i,id});
  store.get<ESM::Door>().values[id].mModel.value=VFS::Path::Normalized(std::to_string(id)+".nif");}
 ++scene.revision;scene.loaded.clear();scene.clones=0;shapes={};preload.preloadStaticExterior(target,3);
 preload.mStaticPreloads->mJobs.run(target);
 assert(scene.loaded.size()==128&&shapes.shapes==128&&shapes.instances==128&&scene.clones==128);
 // Cancellation in reference enumeration prevents resource work.
 ++scene.vfs.revision;scene.loaded.clear();shapes={};reads=0;cancelAt=2;
 preload.preloadStaticExterior(target,4);preload.mStaticPreloads->mJobs.run(target);
 assert(scene.loaded.empty()&&shapes.instances==0);cancelAt=0;
 // Missing/other-format cells and existing live preload jobs do not create runtime cells.
 const auto count=preload.mStaticPreloads->mJobs.submissions;
 preload.preloadStaticExterior({99,99,0},5);preload.preloadStaticExterior({1,2,1},5);
 ExistingCell existing{target};preload.mPreloadCells[&existing]=0;
 ++scene.revision;preload.preloadStaticExterior(target,6);
 assert(preload.mStaticPreloads->mJobs.submissions==count&&worldCalls==0);
 preload.mPreloadCells.clear();preload.mMaxCacheSize=0;preload.preloadStaticExterior(target,7);
 assert(preload.mStaticPreloads->mJobs.submissions==count);
 std::cout<<"PASS: production static resource snapshot resolves moved/deleted/leased winners, owns metadata, bounds models/instances, cancels, refreshes generations and avoids runtime cell access.\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'static.cpp';exe=Path(directory)/'static';path.write_text(code)
    subprocess.run(['g++','-std=c++20','-O2','-I',str(root),str(path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
(root/'static-resource-preload-tests.json').write_text(json.dumps({
    'result':'PASS','production_sha256':hashlib.sha256(source.encode()).hexdigest(),
    'fixture_sha256':hashlib.sha256(code.encode()).hexdigest(),
    'actual_binary_parser':False,'in_game_measured':False
},indent=2)+'\n')
