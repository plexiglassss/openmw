"""Compile the actual PreloadItem::doWork body with counted resource test doubles.
Uses production VFS path types. Does not simulate OSG/Bullet threading or I/O.
"""
from pathlib import Path
import subprocess
import tempfile
import os
source = Path('apps/openmw/mwworld/cellpreloader.cpp').read_text()
start = source.index('        void doWork() override', source.index('class PreloadItem'))
end = source.index('        bool mIsExterior;', start)
body = source[start:end]
preamble = r'''
#include <components/vfs/pathutil.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/sceneutil/preparationcost.hpp>
#include <cassert>
#include <array>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <optional>
#include <iostream>
using VFS::Path::Normalized;
using VFS::Path::NormalizedView;
int corrections=0, actorCorrections=0, templates=0, instances=0, shapes=0, keys=0, attempts=0;
int templatesBeforeFirstKey=-1, renderClones=0, clonesBeforeFirstKey=-1;
std::vector<float> mCollisionScales, preparedScales;
bool failOnce=false, abortOnInstance=false, abortOnKey=false, failKeyOnce=false;
namespace VFS {
struct RecursiveDirectoryIterator {
 std::shared_ptr<std::vector<Normalized>> values;std::size_t index=0;
 const Normalized& operator*()const{return values->at(index);}
 RecursiveDirectoryIterator& operator++(){++index;return *this;}
 bool operator==(const RecursiveDirectoryIterator&)const=default;
};
struct RecursiveDirectoryRange {
 std::shared_ptr<std::vector<Normalized>> values;
 RecursiveDirectoryIterator begin()const{return {values,0};}
 RecursiveDirectoryIterator end()const{return {values,values->size()};}
};
struct Manager {
    std::set<std::string> files;
    mutable int checks=0;
    bool exists(const Normalized& p) const { ++checks; return files.count(p.value()); }
    RecursiveDirectoryRange getRecursiveDirectoryIterator(std::string_view prefix) const {
        std::vector<Normalized> result;
        for (const auto& file : files) if(file.starts_with(prefix)) result.emplace_back(file);
        return {std::make_shared<std::vector<Normalized>>(std::move(result))};
    }
};
}
namespace Misc {
bool useCellRenderPreparation() {return true;}
bool sliced=true;bool useSlicedCellPreload(){return sliced;}
std::string_view getFileName(const Normalized& p) { auto v=p.view(); return v.substr(v.find_last_of('/')+1); }
namespace ResourceHelpers {
Normalized correctMeshPath(NormalizedView path) { ++corrections; return NormalizedView("meshes")/path; }
Normalized correctActorModelPath(const Normalized& path, const VFS::Manager*) {
    ++actorCorrections;
    if(path.value()=="meshes/direct.nif")return Normalized("meshes/xdirect.nif");
    return path.value()=="meshes/alias.nif" ? Normalized("meshes/a.nif") : path;
}
}}
namespace Debug { enum {Warning}; }
std::ostream& Log(int) { return std::cerr; }
struct CellLocation {int mX=0,mY=0;};
std::ostream& operator<<(std::ostream& os,const CellLocation&) {return os;}
std::atomic_bool mAbort{false};
std::shared_ptr<SceneUtil::PreparationSample> mTiming;
struct SceneManager {
 VFS::Manager vfs;
 const VFS::Manager* getVFS() {return &vfs;}
 bool getCachedTemplate(const Normalized&) {return false;}
 int cacheInstance(const Normalized&, bool) { ++renderClones; return 7; }
 int getTemplate(const Normalized&) {
  ++attempts; if(failOnce){failOnce=false;throw std::runtime_error("expected failure");}
  ++templates; return 1;
 }
} scene;
struct Bullet {
 int cacheInstance(const Normalized&,float scale) { ++instances;preparedScales.push_back(scale);if(abortOnInstance)mAbort=true;return 100+instances; }
 int getShape(const Normalized&) { ++shapes; return 2; }
} bullet;
std::set<std::string> requestedKeys;
struct Keyframes { int get(const Normalized& p){requestedKeys.insert(p.value());if(keys==0){templatesBeforeFirstKey=templates;clonesBeforeFirstKey=renderClones;}++keys;if(abortOnKey)mAbort=true;
 if(failKeyOnce){failKeyOnce=false;throw std::runtime_error("expected keyframe failure");}return 3;} } keyframes;
struct Terrain { void cacheCell(int*,int,int){} } terrain;
struct Land {int getLand(CellLocation){return 4;}} land;
auto* mSceneManager=&scene;
auto* mBulletShapeManager=&bullet;
auto* mKeyframeManager=&keyframes;
auto* mTerrain=&terrain;
auto* mLandManager=&land;
std::unique_ptr<int> mTerrainView;
bool mIsExterior=false,mPreloadInstances=true,mPreloadAdditionalAnimations=false;
CellLocation mCellLocation;
int mCellId=0;
std::vector<NormalizedView> mMeshes;
std::vector<NormalizedView> mInstanceMeshes;
std::vector<NormalizedView> mAnimationMeshes;
std::vector<int> mInstanceTickets;
std::set<int> mPreloadedObjects;
std::set<int> mPreloadedOwnedObjects; // RC1 owns non-OSG resources separately; routing doubles remain integer tokens.
struct BaseWork {virtual ~BaseWork()=default;virtual void doWork(){};virtual bool doWorkSlice(){return true;}};
struct PreloadWork:BaseWork {
'''
tests = r'''
};
void run() { mTiming=std::make_shared<SceneUtil::PreparationSample>();mCollisionScales.resize(mMeshes.size(),1.f); PreloadWork work;work.doWork(); }
void reset() {
 corrections=actorCorrections=templates=instances=shapes=keys=attempts=0;
 scene.vfs.checks=0; mAbort=false; failOnce=false; abortOnInstance=false; abortOnKey=false; failKeyOnce=false;
 mTiming=std::make_shared<SceneUtil::PreparationSample>();
 mPreloadAdditionalAnimations=false; mAnimationMeshes.clear(); requestedKeys.clear(); templatesBeforeFirstKey=-1;
 mPreloadInstances=true; mPreloadedObjects.clear(); mPreloadedOwnedObjects.clear(); mInstanceMeshes.clear(); mInstanceTickets.clear(); renderClones=0;clonesBeforeFirstKey=-1;
 mCollisionScales.clear();preparedScales.clear();
 scene.vfs.files={"meshes/a.nif","meshes/xanim.nif","meshes/xanim.kf"};
}
int main() {
 reset();mMeshes={NormalizedView("a.nif"),NormalizedView("alias.nif"),NormalizedView("a.nif")};
 mCollisionScales={0.75f,1.5f,2.f};run();
 assert((preparedScales==std::vector<float>{0.75f,1.5f,2.f}) && templates==1 && instances==3);
 reset(); mMeshes.assign(2000,NormalizedView("a.nif")); run();
 assert(corrections==1 && actorCorrections==1 && scene.vfs.checks==1);
 assert(templates==1 && instances==2000 && shapes==0 && mPreloadedObjects.size()==1 && mPreloadedOwnedObjects.size()==2000);
 assert(mTiming->mComplete.load() && mTiming->mSuccessful && mTiming->mAssets==1 && mTiming->mColdAssets==1);
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
 reset();mPreloadAdditionalAnimations=true;mAnimationMeshes={NormalizedView("a.nif")};mMeshes.assign(20,NormalizedView("a.nif"));
 scene.vfs.files.insert({"animations/a/idle.kf","animations/a/sub/walk.kf","animations/a/bones.nif","animations/a/readme.txt"});
 run();assert(keys==2 && templates==2 && instances==20);
 reset();mMeshes={NormalizedView("a.nif")};
 scene.vfs.files.insert({"animations/a/idle.kf","animations/a/bones.nif"});run();
 assert(keys==0 && templates==1); // comparison/settings-off path retains ordinary loading
 reset();mPreloadAdditionalAnimations=true;mAnimationMeshes={NormalizedView("a.nif")};mMeshes={NormalizedView("a.nif")};
 for(int i=0;i<5000;++i)scene.vfs.files.insert("animations/a/"+std::to_string(i)+".kf");
 run();assert(keys==4096 && templates==1); // bounded work even with huge mod directories
 reset();mPreloadAdditionalAnimations=true;mAnimationMeshes={NormalizedView("a.nif")};mMeshes={NormalizedView("a.nif")};
 scene.vfs.files.insert({"animations/a/first.kf","animations/a/second.kf"});
 abortOnKey=true;run();assert(keys==1 && templates==1);
 reset();mPreloadAdditionalAnimations=true;mAnimationMeshes={NormalizedView("a.nif")};mMeshes={NormalizedView("a.nif")};
 scene.vfs.files.insert({"animations/a/first.kf","animations/a/second.kf"});
 failKeyOnce=true;run();assert(keys==2 && templates==1);
 run();assert(keys==4 && templates==2); // retry in a later job, including the previously failed resource
 reset();mPreloadAdditionalAnimations=true;mAnimationMeshes={NormalizedView("only-kf.nif")};
 mMeshes={NormalizedView("only-kf.nif")};
 scene.vfs.files.insert({"meshes/only-kf.kf","animations/only-kf/extra.kf","animations/only-kf/bones.nif"});
 run();assert(keys==2 && templates==1 && instances==0);
 reset();mPreloadAdditionalAnimations=true;mAnimationMeshes={NormalizedView("a.nif")};
 mMeshes={NormalizedView("a.nif"),NormalizedView("b.nif")};
 scene.vfs.files.insert({"meshes/b.nif","animations/a/idle.kf"});
 mInstanceMeshes=mMeshes;run();assert(templates==2 && templatesBeforeFirstKey==2 && clonesBeforeFirstKey==2 && mInstanceTickets.size()==2); // commit resources finish before optional animations
 reset();mPreloadAdditionalAnimations=true;mAnimationMeshes={NormalizedView("direct.nif")};mMeshes.clear();
 scene.vfs.files.insert({"meshes/direct.kf","meshes/xdirect.kf","animations/direct/extra.kf","animations/xdirect/extra.kf"});
 run();assert(keys==4 && requestedKeys.contains("meshes/direct.kf") && requestedKeys.contains("meshes/xdirect.kf"));
 reset();mPreloadAdditionalAnimations=true;mAnimationMeshes={NormalizedView("meshes/a.nif")};
 mMeshes={NormalizedView("meshes/a.nif")};
 scene.vfs.files.insert({"meshes/a.kf","animations/a/idle.kf"});
 run();assert(templates==1 && instances==1 && keys==2 && requestedKeys.contains("meshes/a.kf"));
 assert(Misc::ResourceHelpers::additionalAnimationPath(NormalizedView("meshes/a.nif"))->value()=="animations/a/");
 reset();mMeshes.assign(2000,NormalizedView("a.nif"));mCollisionScales.resize(mMeshes.size(),1.f);
 PreloadWork slicedWork;assert(!slicedWork.doWorkSlice());
 assert(!mTiming->mComplete.load() && instances<2000);
 while(!slicedWork.doWorkSlice())assert(!mTiming->mComplete.load());
 assert(instances==2000 && templates==1 && mTiming->mComplete && mTiming->mSuccessful && mTiming->mSlices>1);
 reset();mMeshes.assign(2000,NormalizedView("a.nif"));mCollisionScales.resize(mMeshes.size(),1.f);
 PreloadWork cancelled;assert(!cancelled.doWorkSlice());const int before=instances;mAbort=true;
 assert(cancelled.doWorkSlice() && instances==before && mTiming->mComplete && !mTiming->mSuccessful);
 reset();Misc::sliced=false;mMeshes.assign(2000,NormalizedView("a.nif"));mCollisionScales.resize(mMeshes.size(),1.f);
 PreloadWork unsliced;assert(unsliced.doWorkSlice() && instances==2000 && mTiming->mSlices==1);
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
