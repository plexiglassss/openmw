"""Compile the actual PreloadItem::doWork body with counted resource test doubles.
Uses production VFS path types. Does not simulate OSG/Bullet threading or I/O.
"""
from pathlib import Path
import subprocess
import tempfile
import os
source = Path('apps/openmw/mwworld/cellpreloader.cpp').read_text()
start = source.index('        void doWork() override', source.index('class PreloadItem'))
start = source.index('{', start)
end, depth = start + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[start:end]
preamble = r'''
#include <components/vfs/pathutil.hpp>
#include <cassert>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <iostream>
using VFS::Path::Normalized;
using VFS::Path::NormalizedView;
int corrections=0, actorCorrections=0, templates=0, instances=0, shapes=0, keys=0, attempts=0;
bool failOnce=false, abortOnInstance=false;
namespace VFS {
struct Manager {
    std::set<std::string> files;
    mutable int checks=0;
    bool exists(const Normalized& p) const { ++checks; return files.count(p.value()); }
};
}
namespace Misc {
std::string_view getFileName(const Normalized& p) { auto v=p.view(); return v.substr(v.find_last_of('/')+1); }
namespace ResourceHelpers {
Normalized correctMeshPath(NormalizedView path) { ++corrections; return NormalizedView("meshes")/path; }
Normalized correctActorModelPath(const Normalized& path, const VFS::Manager*) {
    ++actorCorrections;
    return path.value()=="meshes/alias.nif" ? Normalized("meshes/a.nif") : path;
}
}}
namespace Debug { enum {Warning}; }
std::ostream& Log(int) { return std::cerr; }
struct CellLocation {int mX=0,mY=0;};
std::ostream& operator<<(std::ostream& os,const CellLocation&) {return os;}
bool mAbort=false;
struct SceneManager {
 VFS::Manager vfs;
 const VFS::Manager* getVFS() {return &vfs;}
 int getTemplate(const Normalized&) {
  ++attempts; if(failOnce){failOnce=false;throw std::runtime_error("expected failure");}
  ++templates; return 1;
 }
} scene;
struct Bullet {
 int cacheInstance(const Normalized&) { ++instances; if(abortOnInstance)mAbort=true; return 100+instances; }
 int getShape(const Normalized&) { ++shapes; return 2; }
} bullet;
struct Keyframes { int get(const Normalized&){++keys;return 3;} } keyframes;
struct Terrain { void cacheCell(int*,int,int){} } terrain;
struct Land {int getLand(CellLocation){return 4;}} land;
auto* mSceneManager=&scene;
auto* mBulletShapeManager=&bullet;
auto* mKeyframeManager=&keyframes;
auto* mTerrain=&terrain;
auto* mLandManager=&land;
std::unique_ptr<int> mTerrainView;
bool mIsExterior=false,mPreloadInstances=true;
CellLocation mCellLocation;
int mCellId=0;
std::vector<NormalizedView> mMeshes;
std::set<int> mPreloadedObjects;
void run()
'''
tests = r'''
void reset() {
 corrections=actorCorrections=templates=instances=shapes=keys=attempts=0;
 scene.vfs.checks=0; mAbort=false; failOnce=false; abortOnInstance=false;
 mPreloadInstances=true; mPreloadedObjects.clear();
 scene.vfs.files={"meshes/a.nif","meshes/xanim.nif","meshes/xanim.kf"};
}
int main() {
 reset(); mMeshes.assign(2000,NormalizedView("a.nif")); run();
 assert(corrections==1 && actorCorrections==1 && scene.vfs.checks==1);
 assert(templates==1 && instances==2000 && shapes==0 && mPreloadedObjects.size()==2001);
 reset(); mPreloadInstances=false; run();
 assert(templates==1 && instances==0 && shapes==1 && corrections==1);
 reset(); mMeshes={NormalizedView("a.nif"),NormalizedView("alias.nif"),NormalizedView("a.nif")}; run();
 assert(corrections==2 && templates==1 && instances==3);
 reset(); mMeshes.assign(20,NormalizedView("missing.nif")); run();
 assert(corrections==1 && templates==0 && instances==0 && scene.vfs.checks==1);
 // A later job sees a newly available path: no process-lifetime negative cache.
 scene.vfs.files.insert("meshes/missing.nif");run();assert(templates==1 && instances==20);
 reset(); mMeshes.assign(20,NormalizedView("xanim.nif")); run();
 assert(keys==1 && templates==1 && instances==20 && scene.vfs.checks==2);
 reset(); mMeshes.assign(2,NormalizedView("a.nif"));failOnce=true;run();
 assert(attempts==2 && templates==1 && instances==1 && corrections==1);
 reset(); mMeshes.assign(20,NormalizedView("a.nif"));mAbort=true;run();assert(corrections==0);
 reset();abortOnInstance=true;run();assert(instances==1 && templates==1);
 std::cout << "PASS: actual preload body preserves instance counts, aliases, keyframes, missing paths, next-job refresh, retries and aborts; 2000 repeated paths resolve once\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'preload.cpp';cpp.write_text(preamble+body+tests)
    exe=Path(tmp)/('preload.exe' if os.name=='nt' else 'preload')
    if os.name=='nt':
        cmd=['cl','/nologo','/std:c++20','/EHsc','/I'+str(Path.cwd()),str(cpp),'/Fe:'+str(exe),'/Fo:'+str(Path(tmp)/'preload.obj')]
    else:
        cmd=['c++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-I.',str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True)
