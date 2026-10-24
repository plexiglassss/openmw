"""Execute production preload admission and door-grid scheduling with counted doubles.
Does not emulate OSG/Bullet concurrency or predict in-game timing.
"""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
def extract(source,signature):
 start=source.index(signature); begin=source.index('{',start); end=begin+1; depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}'); end+=1
 return source[begin:end]
scene=(root/'apps/openmw/mwworld/scene.cpp').read_text()
preload=(root/'apps/openmw/mwworld/cellpreloader.cpp').read_text()
grid=extract(scene,'void Scene::changeCellGrid(')
assert grid.index('mPreloader->preload(cell,') < grid.index('mPreloader->syncTerrainLoad') < grid.index('                loadCell(cell,')
assert 'if (mPreloadEnabled)' in grid
assert grid.count('sortCellsToLoad(')==1
code=r'''
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <vector>
namespace Debug {enum {Error,Warning};}
std::ostream& Log(int) {return std::cerr;}
namespace osg {template<class T> using ref_ptr=std::shared_ptr<T>;}
namespace ESM {using RefId=int; struct ExteriorCellLocation {int x,y,world; ExteriorCellLocation(int a,int b,int c):x(a),y(b),world(c) {}};}
struct Cell {int x=0,y=0;int getGridX(){return x;}int getGridY(){return y;}int getWorldSpace(){return 0;}};
struct CellStore {enum State {State_Unloaded,State_Preloaded,State_Loaded};State state=State_Loaded;bool exterior=true;Cell info;
 State getState(){return state;}bool isExterior(){return exterior;}Cell* getCell(){return &info;}};
using PreloadItemCell=CellStore;
namespace MWWorld {using CellStore=::CellStore;}
struct PreloadItem {bool aborted=false;template<class... Args> explicit PreloadItem(Args&&...) {} void abort(){aborted=true;}};
struct WorkQueue {int jobs=0;void addWorkItem(const osg::ref_ptr<PreloadItem>&){++jobs;}};
struct Resource {int getSceneManager(){return 0;}int getKeyframeManager(){return 0;}};
struct PreloadEntry {double mTimeStamp;osg::ref_ptr<PreloadItem> mWorkItem;PreloadEntry(double t,osg::ref_ptr<PreloadItem> i):mTimeStamp(t),mWorkItem(i){}};
struct CellPreloader {
 WorkQueue queue;WorkQueue* mWorkQueue=&queue;Resource resource;Resource* mResourceSystem=&resource;
 int mBulletShapeManager=0,mTerrain=0,mLandManager=0;bool mPreloadInstances=true;
 using PreloadMap=std::map<const CellStore*,PreloadEntry>;PreloadMap mPreloadCells;
 std::size_t mMaxCacheSize=9;int mAdded=0,mEvicted=0;
 std::size_t getMaxCacheSize(){return mMaxCacheSize;}std::size_t getCacheSize(){return mPreloadCells.size();}
 void preload(CellStore& cell,double timestamp);
};
struct WorldModel {std::map<std::pair<int,int>,CellStore> cells;int lookups=0;
 CellStore& getExterior(ESM::ExteriorCellLocation p){++lookups;auto& c=cells[{p.x,p.y}];c.info={p.x,p.y};return c;}};
struct World {WorldModel model;WorldModel& getWorldModel(){return model;}};
struct Rendering {double time=10;double getReferenceTime(){return time;}};
struct Scene {World mWorld;Rendering mRendering;CellPreloader* mPreloader;int mHalfGridSize=1;
 void preloadCellWithSurroundings(CellStore& cell);};
template<class F> void iterateOverCellsAround(int x,int y,int r,F f){for(int a=x-r;a<=x+r;++a)for(int b=y-r;b<=y+r;++b)f(a,b);}
void sortCellsToLoad(int x,int y,std::vector<std::pair<int,int>>& cells){
 std::stable_sort(cells.begin(),cells.end(),[=](auto a,auto b){return std::abs(a.first-x)+std::abs(a.second-y)<std::abs(b.first-x)+std::abs(b.second-y);});}
void CellPreloader::preload(CellStore& cell,double timestamp)
'''
code+=extract(preload,'void CellPreloader::preload(')
code+='\nvoid Scene::preloadCellWithSurroundings(CellStore& cell)\n'+extract(scene,'void Scene::preloadCellWithSurroundings(')
code+=r'''
int main(){
 CellPreloader p;Scene scene;scene.mPreloader=&p;CellStore center;
 scene.preloadCellWithSurroundings(center);assert(p.queue.jobs==9 && p.getCacheSize()==9);
 scene.mRendering.time=11;scene.preloadCellWithSurroundings(center);
 assert(p.queue.jobs==9);for(auto& [cell,e]:p.mPreloadCells){(void)cell;assert(e.mTimeStamp==11);}
 // Full cache still refreshes hits; no duplicate jobs. A recent cache refuses new destinations.
 CellStore another; p.preload(another,11.5);assert(p.queue.jobs==9 && p.getCacheSize()==9);
 // Old entries are evicted only through the established preloader policy.
 p.preload(another,13);assert(p.queue.jobs==10 && p.mEvicted==1 && p.getCacheSize()==9);
 p.preload(another,14);assert(p.queue.jobs==10 && p.mPreloadCells.at(&another).mTimeStamp==14);
 CellPreloader zero;zero.mMaxCacheSize=0;zero.preload(center,10);assert(zero.queue.jobs==0);
 Scene zeroScene;zeroScene.mPreloader=&zero;zeroScene.preloadCellWithSurroundings(center);assert(zeroScene.mWorld.model.lookups==0);
 CellPreloader limited;limited.mMaxCacheSize=2;Scene small;small.mPreloader=&limited;
 small.preloadCellWithSurroundings(center);assert(limited.queue.jobs==2 && small.mWorld.model.lookups==2);
 small.mRendering.time=12;small.preloadCellWithSurroundings(center);assert(limited.queue.jobs==2);
 for(auto& [cell,e]:limited.mPreloadCells){(void)cell;assert(e.mTimeStamp==12);}
 CellStore interior;interior.exterior=false;CellPreloader inside;Scene indoors;indoors.mPreloader=&inside;
 indoors.preloadCellWithSurroundings(interior);assert(inside.queue.jobs==1 && indoors.mWorld.model.lookups==0);
 std::cout<<"PASS: production door-grid and preload admission: full-cache refresh, no duplicate jobs, eviction, small/zero capacity, interior; transition scheduling precedes terrain wait and insertion.\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 cpp=Path(d)/'test.cpp';exe=Path(d)/'test';cpp.write_text(code)
 subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
