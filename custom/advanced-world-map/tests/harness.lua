local sources = SOURCES
local nativeRequire = require
local modules = {}
local events, messages, logs = {}, {}, {}
local now = 10
local writes, failWrite, throwQueries = 0, nil, false
local saved = {['data.initializer']=6}
local storage = {}
local section = {
  get = function(_, key) return saved[key] end,
  set = function(_, key, value)
    writes = writes + 1
    if writes == failWrite then error('injected storage write failure') end
    saved[key] = value
  end,
  asTable = function() local t={} for k,v in pairs(saved) do t[k]=v end return t end,
  reset = function(_, data) saved=data end,
}
storage.playerSection = function() return section end
local core = {
  getRealTime = function() now=now+0.00001; return now end,
  contentFiles = {list={'Morrowind.esm','fixture.esp'}}, API_REVISION=100,
  sendGlobalEvent = function(name, data) events[#events+1]={name=name,data=data} end,
  land = {getHeightAt=function() return 1 end},
}
local types = {Door={isTeleport=function(d) return d.teleport ~= false end},NPC={records={}},
  Player={sendMenuEvent=function(_,name,data) events[#events+1]={name=name,data=data} end}}
local cells={}
local common={mapDataStorageName='map',configDataSectionName='config',globalNonContentCellsFieldId='noncontent',
  dataInitializerTypes={1,2,3,4,5,6}, exteriorCellIdFormat='#%d %d', exteriorCellLabel='#',
  distance2D=function(a,b) return math.sqrt((a.x-b.x)^2+(a.y-b.y)^2) end,
  doorHash=function(d) return d.id end, isSaveBloatFixed=function() return true end}
local vector=function(x,y,z) return {x=x,y=y,z=z} end
modules['openmw.core']=core
modules['openmw.storage']=storage
modules['openmw.types']=types
modules['openmw.util']={vector2=vector,vector3=vector}
modules['openmw.world']={cells=cells}
modules['scripts.advanced_world_map.common']=common
modules['scripts.advanced_world_map.helpers.protectedDoor']={destCell=function(d)return d.dest end,destPosition=function(d)return d.destPos end}
modules['scripts.advanced_world_map.storage.localStorage']={data={}}
modules['scripts.advanced_world_map.utils.log']=function(s) logs[#logs+1]=s end
modules['scripts.advanced_world_map.config.config']={data={data={safeInit=false}}}
local classifier={
 fillActorRecordData=function() end,
 getDestDoorType=function() end,
 getCellType=function(id) return cells.byId[id].kind end,
 getHighPriorityDoorType=function(d) return d.high end,
 getDoorType=function(d) return d.kind end,
}
modules['scripts.advanced_world_map.mapData.cellDoorType']=classifier
-- Fixture names are ASCII; OpenMW supplies full utf8 on both real runtimes.
utf8 = utf8 or {len=string.len,char=string.char,codepoint=string.byte,offset=function(_,n)return n end,
 codes=function(s) local i=0;return function() i=i+1;if i<=#s then return i,s:byte(i) end end end}
loadstring=loadstring or load
function require(name)
 if modules[name] then return modules[name] end
 local path=name:gsub('^scripts%.advanced_world_map%.',''):gsub('%.','/')..'.lua'
 if sources[path] then
   local value=assert(loadstring(sources[path],path))()
   modules[name]=value
   return value
 end
 return nativeRequire(name)
end
local player={id='player',isValid=function()return true end,
 sendEvent=function(_,name,data) messages[#messages+1]={name=name,data=data} end}
local function cell(id, exterior, x, y)
 local c={id=id,name=id,displayName=id,isExterior=exterior,gridX=x or 0,gridY=y or 0,
   region=exterior and 'Region' or nil,doors={},npcs={},hasTag=function(self) return self.quasi or false end}
 function c:getAll(t)
   if throwQueries then error('injected native query failure') end
   return t==types.Door and self.doors or self.npcs
 end
 cells[#cells+1]=c;cells.byId[id]=c
 return c
end
local function door(a,b,kind)
 local d={id='d'..#a.doors..a.id, dest=b,kind=kind, position=vector(a.gridX*8192+10,#a.doors*50+20,0),
  destPos=vector(b.gridX*8192+30,40,0)}
 a.doors[#a.doors+1]=d
 return d
end
function fixture(kind,n)
 for k in pairs(cells) do cells[k]=nil end
 cells.byId={}
 -- The native world collection has array entries, not our lookup field.
 local collection=setmetatable({}, {__pairs=function() return ipairs(cells) end})
 modules['openmw.world'].cells=collection
 -- LuaJIT OpenMW honors __pairs; emulate its collection iterator here too.
 local originalPairs=pairs
 if not PAIRS_INSTALLED then
   -- Fix otherwise unspecified hash iteration order for differential tests.
   -- This is a boundary fixture, never part of the shipped map scripts.
   _G.pairs=function(t)
     local m=getmetatable(t);if m and m.__pairs then return m.__pairs(t) end
     local keys={} for key in originalPairs(t) do keys[#keys+1]=key end
     table.sort(keys,function(a,b)
       if type(a)=='number' and type(b)=='number' then return a<b end
       local function label(k) return type(k)=='table' and (k.id or tostring(k)) or tostring(k) end
       return label(a)<label(b)
     end)
     local i=0
     return function() i=i+1;local k=keys[i];if k~=nil then return k,t[k] end end
   end
   PAIRS_INSTALLED=true
 end
 if kind=='deep' then
   local previous
   for i=1,n do
    local c=cell('Interior '..i,false)
    if previous then door(previous,c,(i%7==0) and 42 or nil) end
    previous=c
   end
   door(previous,cells[1])
 else
   local outside=cell('Town',true,0,0)
   local inn=cell('Town, Inn',false);local back=cell('Town, Back room',false)
   local other=cell('Other',true,2,1);local quasi=cell('Plaza',false);quasi.quasi=true
   door(outside,inn,42);door(inn,back);door(back,inn);door(back,outside)
   door(inn,other);door(outside,quasi,7);door(quasi,outside)
   local noid=cell('Empty',false);noid.displayName=''
   door(inn,noid)
   local d=door(outside,other);d.teleport=false
   local rec={id='guide',class='guild guide',travelDestinations={{cellId=inn.id,position=vector(0,0,0)},{cellId='#2 1',position=vector(16000,9000,0)}}}
   types.NPC.records={rec}
   outside.npcs={{recordId=rec.id,position=vector(100,200,0)}}
   for i=1,(n or 0) do cell('Village '..i,true,i%12,math.floor(i/12)) end
 end
end
function handler() return assert(loadstring(sources['mapDataHandler.lua'],'mapDataHandler.lua'))() end
function last(list,name)
 for i=#list,1,-1 do if list[i].name==name then return list[i].data end end
end
local function clear(list) for k in pairs(list) do list[k]=nil end end
return {handler=handler,fixture=fixture,events=events,messages=messages,logs=logs,player=player,
 last=last,clear=clear,section=section,saved=function()return saved end,
 failStorage=function(at)writes=0;failWrite=at end,
 failQuery=function(value)throwQueries=value end,
 setSafe=function(value)common.isSaveBloatFixed=function()return not value end;modules['scripts.advanced_world_map.config.config'].data.data.safeInit=value end}
