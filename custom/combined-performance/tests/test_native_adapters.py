"""Bind actual installed-script captures and exercise the changed paths on both supported Lua runtimes."""
import argparse
import gzip
import hashlib
import importlib
import json
from pathlib import Path

import lupa

HERE = Path(__file__).resolve().parent
MODS = HERE.parent / 'mods'

def block(source, start, end):
    return source[source.index(start):source.index(end, source.index(start))]

def check(component, path, source, original, lua):
    # Original and adapter must both compile as complete Lua files. Isolated
    # functions below run without loading a save or changing the user's mods.
    compile_lua = lua.eval('function(s) assert(loadstring(s)) end')
    compile_lua(source)
    compile_lua(original)
    if component == 'ProximityTool':
        a = 'function activeMarker:getDistancesToPlayer()'
        b = '\nend'
        pre = """
        local mt = { __sub = function(a,b) return {length=function()
            return math.sqrt((a.x-b.x)^2+(a.y-b.y)^2+(a.z-b.z)^2) end} end }
        local player = {position=setmetatable({x=2,y=3,z=-5},mt)}
        local nearby = {}
        if native then nearby.measureDistance = function(a,b)
            return (a-b):length(), math.sqrt((a.x-b.x)^2+(a.y-b.y)^2), a.z-b.z end end
        local activeMarker = { getClosestPos=function() calls=calls+1
            if missing then return nil end
            return {x=4,y=8,z=1}, 'object' end }
        """
        for native in (False, True):
            lua.globals().native = native
            for missing in (False, True):
                lua.globals().missing = missing
                values = []
                for script in (original, source):
                    lua.globals().calls = 0
                    values.append(lua.execute(pre + block(script, a, b) + '\nend\nreturn activeMarker:getDistancesToPlayer()'))
                    assert lua.globals().calls == 1
                assert values[0] == values[1]
    elif component == 'AdvancedWorldMap' and path.endswith('/player.lua'):
        a, b = 'discoverNearby = function()', 'time.runRepeatedly(discoverNearby'
        pre = """
        local log = {}
        local configLib = {data={main={discoveryRadius=5}}}
        local mt={__sub=function(a,b) return {length=function() return math.abs(a.x-b.x) end} end}
        local self={position=setmetatable({x=0},mt), cell='home'}
        local function door(x,id,teleport) return {position=setmetatable({x=x},mt),id=id,t=teleport} end
        local nearbyDoors={door(5,'edge',true), door(6,'out',true), door(-1,'disabled',false), door(2,'inside',true)}
        local types={Door={isTeleport=function(d) return d.t end}}
        local pDoor={destCell=function(d) return {id=d.id} end}
        local discoveredLocs={isDiscovered=function(id) return false end,
          addDiscoveredCell=function(c) return c.id end, discoverPosition=function() return true end}
        local markers={updateDiscovered=function(id) log[#log+1]=id end}
        local mapMenu={activeMenuMeta=true,updateFog=function() log[#log+1]='fog' end}
        local nearby={}
        if native then nearby.queryDistances=function(ds,o,opts)
          local out={object=function(self,i) return self[i] end}
          for _,d in ipairs(ds) do if (d.position-o):length()<=opts.maxDistance then out[#out+1]=d end end
          return out end end
        """
        for native in (False, True):
            lua.globals().native = native
            for script in (original, source):
                assert lua.execute(pre + block(script,a,b) + "discoverNearby();return table.concat(log,',')") == 'edge,inside,fog'
    elif component == 'AdvancedWorldMap':
        pre = """
        local trace = {}
        local Door = {objectIsInstance=function(ref) return ref=='door' end}
        local types = {NPC={}}
        local cell = {getAll=function(self,t) return {t==Door and 'door' or 'npc'} end}
        if native then cell.getAllOfTypes=function() return {'door','npc'}, {1,1} end end
        local function checkDoor(ref) trace[#trace+1]=ref end
        local function checkActor(ref) trace[#trace+1]=ref end
        """
        f=block(source,'local function checkCellObjects','\nend')+'\nend'
        for native in (False, True):
            lua.globals().native = native
            assert lua.execute(pre + f + "\ncheckCellObjects(cell,{});return table.concat(trace,',')") == 'door,npc'
    else:
        f=block(source,'local supportedGiverTypeList','local function onInit')
        pre = """
        local supportedGiverTypes={a=true,b=true,c=true}
        local cell={getAll=function(self,t) return {t..'1',t..'2'} end}
        if native then cell.getAllOfTypes=function(self,ts)
            if yielding then error('queried all types before a possible yield') end
            local r,c={},{}
            for i,t in ipairs(ts) do for _,v in ipairs(self:getAll(t)) do r[#r+1]=v end; c[i]=2 end
            return r,c end end
        """
        run = """
        local a,b={},{}
        local function visit(v) a[#a+1]=v end
        local function checkpoint() a[#a+1]='tick' end
        forEachSupportedGiver(cell, visit, yielding and checkpoint or nil)
        for t in pairs(supportedGiverTypes) do
            if yielding then b[#b+1]='tick' end
            for _,v in pairs(cell:getAll(t)) do b[#b+1]=v end
        end
        assert(table.concat(a,',')==table.concat(b,','))
        """
        for native in (False,True):
            for yielding in (False,True):
                lua.globals().native, lua.globals().yielding = native, yielding
                lua.execute(pre+f+run)

def main(output):
    cases=[]
    for rt in ('lupa.lua51','lupa.luajit21'):
        lua=importlib.import_module(rt).LuaRuntime(unpack_returned_tuples=True)
        for component in ('ProximityTool','AdvancedWorldMap','QuestGuiderLite'):
            manifest=json.loads((MODS/component/'manifest.json').read_text())
            for row in manifest['files']:
                path=row['path']
                original=gzip.decompress((HERE/'fixtures/NativeJobAdapters'/(component+'-'+Path(path).name+'.gz')).read_bytes())
                source=(MODS/component/path).read_bytes()
                assert hashlib.sha256(original).hexdigest()==row['expected_sha256']
                assert hashlib.sha256(source).hexdigest()==row['installed_sha256']
                check(component,path,source.decode('utf-8-sig'),original.decode('utf-8-sig'),lua)
                cases.append(dict(component=component,path=path,runtime=rt,result='PASS',
                                  baseline_sha256=row['expected_sha256'],adapter_sha256=row['installed_sha256']))
    result=dict(result='PASS',lupa_version=lupa.__version__,cases=cases)
    output.write_text(json.dumps(result,indent=2)+'\n')
    print(f'{len(cases)} adapter/runtime pairs passed the native and fallback scenarios')

if __name__=='__main__':
    arg=argparse.ArgumentParser()
    arg.add_argument('--output',required=True,type=Path)
    main(arg.parse_args().output)
