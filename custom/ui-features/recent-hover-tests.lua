local root='custom/ui-features/InventoryExtenderIntegration/Data_Files/'
package.path=root..'?.lua;'..package.path
local recent=require('scripts.InventoryExtender.util.recentItems')
local player={id='player'}
local a={id='a',recordId='potion',count=1,parentContainer=player,isValid=function()return true end}
local b={id='b',recordId='sword',count=1,parentContainer=player,isValid=function()return true end}
local function mark()
 recent.load(nil);recent.observe({});recent.observe({a,b})
end
local function tick(item,n)
 for i=1,n do recent.updateHover(item,0.1) end
end
mark();tick(a,10);assert(recent.isRecent(a),'cleared before a full second after focus began')
assert(recent.updateHover(a,0.1));assert(not recent.isRecent(a) and recent.isRecent(b))
recent.observe({a,b});assert(not recent.isRecent(a),'unchanged count re-marked')
mark();tick(a,6);tick(nil,1);tick(a,10);assert(recent.isRecent(a));tick(a,1);assert(not recent.isRecent(a))
mark();tick(a,6);tick(b,6);tick(a,10);assert(recent.isRecent(a));tick(a,1);assert(not recent.isRecent(a))
mark();tick(a,6);local saved=recent.save();recent.load(saved);tick(a,10);assert(recent.isRecent(a))
mark();tick(a,6);recent.updateHover(a,2);tick(a,10);assert(recent.isRecent(a),'stall counted as hover')
mark();tick(a,6);recent.clear();tick(a,20);assert(not recent.isRecent(a))
local sibling={id='otherStack',recordId='potion',count=1}
mark();tick(a,6);tick(sibling,10);assert(recent.isRecent(a));tick(sibling,1);assert(not recent.isRecent(a))
-- Exercise the actual window integration, not just the timer in isolation.
local f=assert(io.open(root..'scripts/InventoryExtender/ui/windowManager.lua'));local source=f:read('*a');f:close()
local first=assert(source:find('    local hoveredItem',1,true))
local last=assert(source:find('    for _, window in pairs(self.windows) do\n        -- Clear stale hovered row pos',first,true))
local ctx={dragAndDrop={},activeTooltip={layout={props={},userData={recentItem=a}}}}
local refreshes=0
local mode='Interface'
local env=setmetatable({self={ctx=ctx,refresh=function()refreshes=refreshes+1 end},
 recent=recent,dt=0.1,omwself=player,validInventoryModes={Interface=true,Container=true},
 I={UI={getMode=function()return mode end}},configPlayer={tweaks={}}},{__index=_G})
local frame=assert(load(source:sub(first,last-1),'shipped hover integration','t',env))
local function frames(n)for i=1,n do frame()end end
mark();frames(11);assert(not recent.isRecent(a) and refreshes==1)
for _,condition in ipairs({'hidden','modal','drag','closed','foreign','invalid','disabled'})do
 mark();frames(5)
 if condition=='hidden' then ctx.activeTooltip.layout.props.visible=false
 elseif condition=='modal' then ctx.modalElement={}
 elseif condition=='drag' then ctx.dragAndDrop.draggingObject=a
 elseif condition=='closed' then mode=nil
 elseif condition=='foreign' then a.parentContainer={id='chest'}
 elseif condition=='invalid' then a.isValid=function()return false end
 elseif condition=='disabled' then env.configPlayer.tweaks.b_RecentItems=false end
 frames(20);assert(recent.isRecent(a),condition..' cleared a marker')
 ctx.activeTooltip.layout.props.visible=nil;ctx.modalElement=nil;ctx.dragAndDrop.draggingObject=nil
 mode='Interface';a.parentContainer=player;a.isValid=function()return true end;env.configPlayer.tweaks.b_RecentItems=true
 frames(10);assert(recent.isRecent(a),condition..' retained earlier hover time');frames(1);assert(not recent.isRecent(a))
end
print('PASS: one-second continuous focus, leave/switch/stack/reset/stall handling, ownership, hidden/modal/drag/closed cases, one label refresh')

-- Execute the production normalizer on the layouts used by IE and Sun's Dusk.
do
 local f=assert(io.open(root..'scripts/InventoryExtender/ui/templates/magic.lua'))
 local source=f:read('*a');f:close()
 local first=assert(source:find('local function normalizeTooltipDividers',1,true))
 local last=assert(source:find('Templates.itemTooltip =',first,true))
 local line={}
 local env=setmetatable({I={MWUI={templates={horizontalLine=line}}},v2=function(x,y)return{x=x,y=y}end},{__index=_G})
 local normalize=assert(load(source:sub(first,last-1)..'\nreturn normalizeTooltipDividers','shipped divider normalizer','t',env))()
 local header={template=line,external={stretch=1}}
 local thermal={template=line,props={size={x=200,y=2}}}
 local lore={template=line,props={size={x=280,y=2},position={x=20,y=0}}}
 local text={props={text='Temperature Properties',size={x=320,y=0}}}
 local layout={content={{content={header}},thermal,lore,text}}
 normalize(layout)
 for _,divider in ipairs({header,thermal,lore})do
  assert(divider.template==line and divider.external.stretch==1)
  assert(divider.props.size.x==0 and divider.props.size.y==2 and divider.props.position.x==0)
 end
 assert(#layout.content==4 and text.props.text=='Temperature Properties' and text.props.size.x==320)
 normalize(layout);assert(#layout.content==4,'normalization duplicates sections')
 assert(source:find('userData = { recentItem = item }',1,true),'comparison lost primary hover target')
 print('PASS: icon/temperature/lore dividers use one stretch policy; provider text and conditional sections preserved')
end
