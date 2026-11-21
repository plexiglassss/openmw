"""Run the production chunk-cache method with real OSG ownership and counted construction."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--include-dir')
parser.add_argument('--lib-dir')
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
source=(root/'components/terrain/chunkmanager.cpp').read_text()

def method(signature):
    start=source.index(signature)
    brace=source.index('{',start)
    end=brace+1
    depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    return source[start:end]

code=r'''
#include <components/terrain/chunkmanager.hpp>
#include <components/misc/combinedperformance.hpp>
#include <components/diagnostics/loading.hpp>
#include <components/diagnostics/timing.hpp>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <thread>
using Terrain::ChunkKey;using Terrain::TemplateKey;using TerrainDrawable=osg::Geometry;
struct ChunkManager:Resource::NodeResourceManager<ChunkKey>{
 std::shared_mutex mBuildMutex;Misc::SingleFlight<ChunkKey,osg::ref_ptr<osg::Node>> mPending;
 std::atomic_uint calls{0};std::function<void()> during;Terrain::BufferCache mBufferCache;
 struct Vertices{void clear(){}}mVertexCache;
 ChunkManager():Resource::NodeResourceManager<ChunkKey>(nullptr,5){}
 auto lookups()const{return mCache->getStats().mGet;}
 osg::ref_ptr<osg::Node> createChunk(float,const osg::Vec2f&,unsigned char,unsigned,bool,const TerrainDrawable*){
  ++calls;if(during)during();return new TerrainDrawable;
 }
 osg::ref_ptr<osg::Node> getChunk(float,const osg::Vec2f&,unsigned char,unsigned,bool,const osg::Vec3f&,bool,const std::atomic_bool* =nullptr);
 void clearCache()override;
};
''' + method('    osg::ref_ptr<osg::Node> ChunkManager::getChunk(') + '\n' + method('    void ChunkManager::clearCache()') + r'''
int main(){
 ChunkManager manager;const osg::Vec2f center{.5f,.5f};
 const auto get=[&](float size,unsigned flags=0,bool compile=false){return manager.getChunk(size,center,0,flags,false,{},compile);};
 const auto first=get(1);assert(first==get(1));assert(first!=get(2));assert(first!=get(1,1));assert(manager.calls==3);
 manager.clearCache();assert(first!=get(1));assert(manager.calls==4);
 manager.clearCache();manager.during=[] {throw std::runtime_error("injected construction failure");};
 try{get(1);assert(false);}catch(const std::runtime_error&){}
 manager.during={};assert(get(1));assert(manager.calls==6);
 if(Misc::useTerrainPreparation()){
  manager.clearCache();std::mutex mutex;std::condition_variable changed;bool release=false;
  manager.during=[&]{std::unique_lock lock(mutex);if(!changed.wait_for(lock,std::chrono::seconds(5),[&]{return release;}))throw std::runtime_error("gate timeout");};
  const auto before=manager.lookups();std::vector<std::future<osg::ref_ptr<osg::Node>>> results;
  for(unsigned i=0;i<8;++i)results.push_back(std::async(std::launch::async,[&,i]{return get(1,0,i%2); }));
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(manager.lookups()<before+9 && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  assert(manager.lookups()>=before+9);
  {std::lock_guard lock(mutex);release=true;changed.notify_all();}
  const auto shared=results[0].get();for(unsigned i=1;i<8;++i)assert(results[i].get()==shared);
  assert(manager.calls==7);manager.during={};
 }
 std::cout<<"PASS: production chunk cache coalesces exact requests, distinguishes size/topology, clears and retries construction errors\n";
}
'''
with tempfile.TemporaryDirectory(prefix='terrain-chunks-') as temp:
    path=Path(temp);cpp=path/'chunks.cpp';cpp.write_text(code);exe=path/('chunks.exe' if os.name=='nt' else 'chunks')
    if os.name=='nt':
        if not args.include_dir or not args.lib_dir:parser.error('Windows requires OSG include/lib directories')
        command=['cl','/nologo','/std:c++20','/EHsc','/O2','/UNDEBUG','/I'+str(root),'/I'+args.include_dir,
                 str(cpp),str(root/'components/terrain/buffercache.cpp'),'/Fe:'+str(exe),'/link','/LIBPATH:'+args.lib_dir,'osg.lib','OpenThreads.lib']
    else:
        command=['g++','-std=c++20','-O2','-UNDEBUG','-pthread','-I'+str(root)]
        if args.include_dir:command+=['-isystem',args.include_dir]
        command+=[str(cpp),str(root/'components/terrain/buffercache.cpp')]
        if args.lib_dir:command+=['-L'+args.lib_dir,'-Wl,-rpath,'+args.lib_dir,'-Wl,-rpath-link,'+args.lib_dir]
        command+=['-losg','-lOpenThreads','-o',str(exe)]
    subprocess.run(command,check=True,cwd=path)
    for name,flag in [('enabled','1'),('disabled','0')]:
        subprocess.run([str(exe)],env=dict(os.environ,OPENMW_COMBINED_PERFORMANCE='1',OPENMW_TERRAIN_PREPARATION=flag),check=True)
    (root/'terrain-chunk-tests.json').write_text(json.dumps({'source_sha256':hashlib.sha256(source.encode()).hexdigest(),
        'modes_passed':['enabled','disabled'],'scope':'Production getChunk/clearCache methods, actual keys/cache/SingleFlight and OSG lifetimes; counted node construction.'},indent=2)+'\n')
