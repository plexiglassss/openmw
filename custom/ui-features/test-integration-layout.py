"""Run shipped layout and drag callbacks, rather than a duplicate layout formula."""
from pathlib import Path
import ctypes as c
root=Path('custom/ui-features/InventoryExtenderIntegration/Data_Files/scripts/InventoryExtender')
s=(root/'ui/windows/inventory.lua').read_text()
layout=s[s.index('    self.updateSize = function(self)'):s.index('    content:add(self.itemTable)',s.index('    self.updateSize = function(self)'))]
b=(root/'ui/templates/base.lua').read_text()
start=b.index('            mouseMove = async:callback(function(e, layout)',b.index('Templates.window ='))
end=b.index('            focusGain =',start)
drag=b[start:end].replace('            mouseMove = async:callback(function(e, layout)','local mouseMove = function(e, layout)',1).rstrip()
assert drag.endswith('end),');drag=drag[:-len('end),')]+'end\n'
pre='''
local mt={}
local function v2(x,y)return setmetatable({x=x,y=y},mt)end
mt.__add=function(a,b)return v2(a.x+b.x,a.y+b.y)end
mt.__sub=function(a,b)return v2(a.x-b.x,a.y-b.y)end
local util={vector2=v2,clamp=function(x,a,b)return math.max(a,math.min(b,x))end}
local configPlayer={window={b_FullHeightPaperDoll=true}}
local inner=v2(800,600)
local function element(h)
 return {layout={props={size=v2(0,h),position=v2(0,0)},userData={}},update=function()end}
end
local self={element=element(0),paperDoll={props={}},paperDollDivider={props={}},paperDollVisible=true,
 categoryFilter=element(80),infoBar=element(24),itemTable=element(0),infoBarDivider={props={}},categoryFilterDivider={props={}}}
self.element.layout.userData.getInnerSize=function()return inner end
self.categoryFilter.layout.userData.resizeImages=function(w)self.filterWidth=w end
self.itemTable.layout.userData.resize=function(size)self.itemTable.layout.props.size=size end
'''
checks='''
self:updateSize()
assert(self.categoryFilter.layout.props.position.x>0 and self.infoBar.layout.props.position.x>0)
assert(self.paperDoll.props.position.y==0 and self.paperDollDivider.props.position.y==0)
assert(self.paperDollDivider.props.size.y==inner.y)
local first=self.paperDoll.props.size.y
inner=v2(1100,850);self:updateSize();assert(self.paperDoll.props.size.y>first,'portrait must grow with window')
assert(self.paperDoll.props.size.y==self.paperDoll.props.size.x*2)
configPlayer.window.b_FullHeightPaperDoll=false;self:updateSize()
assert(self.categoryFilter.layout.props.position.x==0 and self.infoBar.layout.props.position.x==0)
assert(self.paperDoll.props.position.y==80 and self.paperDollDivider.props.position.y==80)
self.paperDollVisible=false;self:updateSize();assert(self.paperDoll.props.visible==false)
assert(self.paperDollDivider.props.visible==false and self.itemTable.layout.props.position.x==0)
assert(self.itemTable.layout.props.size.x==inner.x)
for _,size in ipairs({v2(200,60),v2(400,250),v2(1400,1000)})do
 inner=size;self.paperDollVisible=true
 for _,full in ipairs({true,false})do
  configPlayer.window.b_FullHeightPaperDoll=full;self:updateSize()
  assert(self.paperDoll.props.size.x>0 and self.itemTable.layout.props.size.x>0)
 end
end
local constants={DragType={Move=1,ResizeL=2,ResizeR=3,ResizeT=4,ResizeB=5,ResizeTL=6,ResizeTR=7,ResizeBL=8,ResizeBR=9}}
local userData={dragging=true,dragStartAbs=v2(0,0),dragStartSize=v2(300,400),dragStartPos=v2(100,100),dragType=1}
local ctx={}
local calls,enabled=0,true
local ui={layers={[1]={size=v2(1920,1080)},indexOf=function()return 1 end}}
local window=element(400)
local onDrag=function()end
local minWidth,minHeight=200,60
ui.snapWindow=function(w,pos,size)
 assert(w==window and size.x==300)
 calls=calls+1
 return enabled and v2(500,pos.y) or pos
end
'''
endchecks='''
mouseMove({position=v2(390,0)},window.layout)
assert(calls==1 and window.layout.props.position.x==500,'custom IE move handler must call engine snapping')
enabled=false;mouseMove({position=v2(390,0)},window.layout)
assert(window.layout.props.position.x==490,'toggle off must keep raw pointer position')
ui.snapWindow=nil;mouseMove({position=v2(390,0)},window.layout)
assert(window.layout.props.position.x==490,'older engine fallback')
userData.dragType=constants.DragType.ResizeR;mouseMove({position=v2(30,0)},window.layout)
assert(calls==2 and window.layout.props.size.x==330,'resizing must not snap')
print('PASS: shipped portrait resize/full-height toggle and custom IE drag snapping callback')
'''
lua=c.CDLL('liblua5.4.so.0');lua.luaL_newstate.restype=c.c_void_p
lua.luaL_openlibs.argtypes=[c.c_void_p];lua.luaL_loadstring.argtypes=[c.c_void_p,c.c_char_p]
lua.lua_pcallk.argtypes=[c.c_void_p,c.c_int,c.c_int,c.c_int,c.c_longlong,c.c_void_p]
lua.lua_tolstring.argtypes=[c.c_void_p,c.c_int,c.c_void_p];lua.lua_tolstring.restype=c.c_char_p
lua.lua_close.argtypes=[c.c_void_p]
state=lua.luaL_newstate();lua.luaL_openlibs(state)
status=lua.luaL_loadstring(state,(pre+layout+checks+drag+endchecks).encode())
if not status:status=lua.lua_pcallk(state,0,0,0,0,None)
if status:raise RuntimeError(lua.lua_tolstring(state,-1,None).decode())
lua.lua_close(state)
assert "text = l10n('UI_RecentBadge')" not in s
assert "l10n('UI_RecentBadge') .." not in s
assert s.count("icons/InventoryExtender/custom/recent.png")==2
print('PASS: table/grid recent markers use the packaged image rather than NEW text')

assert 'paperDollToggle' not in s
