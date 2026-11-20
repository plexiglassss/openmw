"""Run production map builders against v11 with mocked native/storage boundaries.
The map graph, DFS, clustering, rectangles and transport are actual Lua code.
These are correctness tests, not an in-game frame-time measurement.
"""
import argparse,gzip,importlib,json,hashlib
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parent/'Advanced World Map - Rebuild Fix/scripts/advanced_world_map'
BASE=json.loads(gzip.decompress((HERE/'upstream-v1.18.0.json.gz').read_bytes()))
KEYS='cellNameData regionNameData entrances cellNameById validCellsWithoutName grid worldMapTileRectangles transport cellCount contentFileCount'.split()

def plain(value):
    if hasattr(value,'items'):
        pairs={str(k):plain(v) for k,v in value.items()}
        return dict(sorted(pairs.items()))
    return value

def setup(runtime,patched,kind='shallow',n=0):
    lua=importlib.import_module('lupa.'+runtime).LuaRuntime(unpack_returned_tuples=True)
    sources=dict(BASE)
    if patched:
        sources.update({str(p.relative_to(ROOT)):p.read_text() for p in ROOT.rglob('*.lua')})
    for name,source in sources.items():
        lua.eval('function(source,name) return assert((loadstring or load)(source,name)) end')(source,name)
    lua.globals().SOURCES=lua.table_from(sources)
    env=lua.execute((HERE/'harness.lua').read_text())
    env.fixture(kind,n)
    return lua,env,env.handler()

def run(runtime):
    checks=[]
    def mark(name):checks.append(name)
    def handshake(lua,e,g,p,**options):
        p.requestInit(e.player,lua.table_from(options))
        request=e.last(e.events,'AdvWMap:initMapData')
        g.globalInit(e.player,request.options)
        reply=e.last(e.messages,'AdvWMap:initMapData')
        p.playerInit(e.player,reply.cellCount,reply.options)
        return reply.options
    def finish(e,g):
        for i in range(200000):
            g.advanceBuild()
            payload=e.last(e.messages,'AdvWMap:updateMapData')
            if payload:return payload,i+1
        raise AssertionError('map job did not finish: '+str(plain(e.logs)))
    # Deep chains used to exhaust the recursive processCell call stack.
    for kind,n in [('shallow',30),('deep',12000)]:
        lua,e,g=setup(runtime,True,kind,n);p=e.handler()
        options=handshake(lua,e,g,p,force=True,buildCellWorldMapData=True)
        assert g.globalBuildData(e.player,options)
        assert not g.isInitialized() and not p.isInitialized()
        assert not g.globalBuildData(e.player,options), 'duplicate start must be inert'
        g.advanceBuild();assert not g.isInitialized()
        data,slices=finish(e,g);assert slices>1
        assert not g.isInitialized() and not p.isInitialized(), 'only complete acknowledged results become visible'
        assert p.updateData(e.player,data)
        assert not p.updateData(e.player,data), "duplicate result must not repeat storage or menu success"
        assert g.commitBuild(data._rebuildToken)
        assert g.isInitialized() and p.isInitialized()
        assert not g.commitBuild(data._rebuildToken)
        if kind=='shallow':
            oldlua,old,baseline=setup(runtime,False,kind,n)
            baseline.globalBuildData(old.player,oldlua.table_from({'force':True,'buildCellWorldMapData':True}))
            expected=old.last(old.messages,'AdvWMap:updateMapData')
            for key in KEYS:
                if key == "contentFileCount":
                    assert data[key] == 2  # v11 populated this only when committing storage.
                    continue
                assert plain(data[key])==plain(expected[key]), (key,plain(data[key]),plain(expected[key]))
            mark('v11 differential graph/classification/transport/clustering/rectangle parity')
        else:
            assert len(list(data.cellNameById.items()))==12000
            assert sum(len(list(edges.items())) for _,edges in data.entrances.items())==12000
            mark('12000-cell chain and cycle complete over multiple paused-compatible update slices')
    lua,e,g=setup(runtime,True,'shallow',50);p=e.handler()
    options=handshake(lua,e,g,p,force=True)
    g.globalBuildData(e.player,options);data,_=finish(e,g)
    p.updateData(e.player,data);g.commitBuild(data._rebuildToken)
    old=p.cellNameById
    # Supersession invalidates old results immediately, before a global reply.
    p.requestInit(e.player,lua.table_from({'force':True}))
    assert not p.updateData(e.player,data)
    reply=e.last(e.messages,'AdvWMap:initMapData')
    before=len(list(e.events.items()))
    assert not p.playerInit(e.player,reply.cellCount,reply.options)
    assert len(list(e.events.items()))==before and not p.isCurrentRequest(reply.options._rebuildToken)
    assert plain(p.cellNameById)==plain(old)
    p.resetSession();assert not p.playerInit(e.player,reply.cellCount,reply.options)
    assert len(list(e.events.items()))==before and not p.isCurrentRequest(reply.options._rebuildToken)
    assert not p.isInitialized()
    mark('stale init/results ignored during supersession and after load reset')
    e.clear(e.messages)
    options=handshake(lua,e,g,p,force=True)
    before=len(list(e.events.items()))
    assert not p.playerInit(e.player,0,options), 'duplicate init must be inert'
    assert len(list(e.events.items()))==before
    assert g.globalBuildData(e.player,options)
    # Cache a good published map, then fail halfway through a new storage commit.
    data,_=finish(e,g)
    old_saved=plain(e.saved());e.failStorage(3)
    assert not p.updateData(e.player,data)
    assert plain(e.saved())==old_saved and not p.isInitialized()
    e.failStorage(None);assert p.updateData(e.player,data);g.commitBuild(data._rebuildToken)
    mark('partial storage failure restores old storage and does not announce success')
    e.clear(e.messages)
    options=handshake(lua,e,g,p,force=True)
    old_global=plain(g.cellNameById);old_player=plain(p.cellNameById)
    g.globalBuildData(e.player,options);e.failQuery(True)
    for i in range(10000):g.advanceBuild()
    assert e.last(e.messages,'AdvWMap:updateMapData') is None
    assert plain(g.cellNameById)==old_global and plain(p.cellNameById)==old_player
    mark('native query failure retains both published maps')
    e.failQuery(False);e.clear(e.messages)
    options=handshake(lua,e,g,p,force=True);g.globalBuildData(e.player,options)
    g.advanceBuild();g.resetSession()
    for i in range(100):g.advanceBuild()
    assert e.last(e.messages,'AdvWMap:updateMapData') is None
    assert not g.commitBuild(options._rebuildToken)
    mark('load cancels a partially built result and rejects its later commit')
    # Safe-init starts can arrive late from independent menu timers.
    for reverse in (False,True):
        lua,e,g=setup(runtime,True,'shallow',10);p=e.handler();e.setSafe(True)
        a=handshake(lua,e,g,p,force=True)
        b=handshake(lua,e,g,p,force=True)
        assert e.last(e.events,'AdvWMap:startDataRebuilding') is not None
        if reverse:
            assert g.globalBuildData(e.player,b)
            assert not g.globalBuildData(e.player,a)
        else:
            assert g.globalBuildData(e.player,a)
            assert g.globalBuildData(e.player,b)
        data,_=finish(e,g)
        assert data._rebuildToken==b._rebuildToken and p.updateData(e.player,data)
        assert e.last(e.events,'AdvWMap:finishDataRebuilding').options._rebuildToken==b._rebuildToken
        assert g.commitBuild(b._rebuildToken)
        assert not g.loadMapData(lua.table_from({'_rebuildToken':a._rebuildToken}))
        # Cache-only init must converge without another build, then reject replay.
        e.clear(e.events);e.clear(e.messages)
        c=handshake(lua,e,g,p,skipRebuild=True)
        cache=e.last(e.events,'AdvWMap:updateMapData')
        assert cache is not None
        g.loadMapData(cache)
        assert g.isCurrentRequest(c._rebuildToken)
        assert not g.loadMapData(cache)
    mark('both delayed safe-init orders converge to newest request; stale cache and duplicate results are inert')
    lua,e,g=setup(runtime,True)
    lua.execute("""
      local make=require('scripts.advanced_world_map.rebuildJob').new
      local time,count=0,0
      local function clock()time=time+0.0005;return time end
      local job=make(function(checkpoint)for i=1,100 do checkpoint();count=count+1 end return count end,clock,0.002,10000)
      assert(job.step()=='pending' and count>0 and count<10) -- time, not operation cap
      job.cancel();local before=count;assert(job.step()=='cancelled' and count==before)
      local failed=make(function()error('expected')end,function()return 0 end,0.002,128)
      assert(failed.step()=='failed')
    """)
    mark('time budget yields independently of operation cap; cancellation and coroutine failure are terminal')
    lua.globals().MENU_SOURCE=(ROOT/'dataRebuildService.lua').read_text()
    lua.execute("""
      local callbacks,operations,sends={},{},{}
      local menu={
        saveGame=function()operations[#operations+1]='save'end,
        loadGame=function()operations[#operations+1]='load'end,
        deleteGame=function()operations[#operations+1]='delete'end,
        getCurrentSaveDir=function()return 'saves'end,
        getSaves=function()return {['__advWMap__temp__save__1']={}}end,
      }
      local core={sendGlobalEvent=function(name,data)sends[#sends+1]={name=name,data=data}end}
      local async={newUnsavableSimulationTimer=function(_,_,fn)callbacks[#callbacks+1]=fn end}
      local original=require
      require=function(n)if n=='openmw.menu'then return menu elseif n=='openmw.core'then return core elseif n=='openmw.async'then return async end return original(n)end
      local service=assert((loadstring or load)(MENU_SOURCE))()
      local data={plId='player',options={force=true,_rebuildToken='old',_requestToken='request'}}
      service.eventHandlers['AdvWMap:startDataRebuilding'](data)
      assert(operations[1]=='save' and #sends==0);callbacks[1]()
      assert(sends[1].name=='AdvWMap:rebuildMapData' and sends[1].data==data)
      service.eventHandlers['AdvWMap:finishDataRebuilding'](data)
      assert(operations[2]=='load');callbacks[2]()
      assert(operations[3]=='delete' and sends[2].name=='AdvWMap:requestMapData')
      assert(sends[2].data.options.skipRebuild and sends[2].data.options.force==false and sends[2].data.options._rebuildToken==nil)
      assert(data.options.force and data.options._rebuildToken=='old')
      require=original
    """)
    mark('actual safe-init menu module saves/reloads and requests a fresh handshake without mutating prior options')
    return checks

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output');args=parser.parse_args()
    result={'result':'PASS','scope':__doc__,'runtimes':{}}
    for runtime in ('lua54','luajit21'):
        result['runtimes'][runtime]=run(runtime)
    result['source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in ROOT.rglob('*.lua')}
    if args.output:Path(args.output).write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
