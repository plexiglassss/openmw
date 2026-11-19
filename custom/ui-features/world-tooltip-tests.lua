local solid = {}
package.preload['scripts.InventoryExtender.ui.templates.base']=function()return {boxSolid=solid}end
local calls, builds, destroys, time = {}, 0, 0, 0
local focused, fail
local suppressed
local ui = {
    TYPE={Flex='Flex'}, content=function(x)return x end,
    getWorldTooltipObject = function() return focused end,
    setWorldTooltip = function(obj, element) calls[#calls+1] = {obj,element} end,
    create = function(layout)
        return {layout=layout,destroy=function() destroys=destroys+1 end}
    end,
}
package.preload['openmw.interfaces'] = function() return {ObjectInsightTooltip={
    nativeSupplement=function(obj)return {statsFor=obj} end,
    setNativeTooltipObject=function(obj)suppressed=obj end,
}} end
package.preload['openmw.ui'] = function() return ui end
package.preload['openmw.core'] = function() return {getRealTime=function() return time end} end
package.preload['openmw.types'] = function() return {Item={objectIsInstance=function(o)return o.item end}} end
local mode, alt, equipped, slots, alreadyEquipped = 'Never', false, {}, {'right'}, false
local comparisons, lookups, compareFail = 0, 0, false
local actor, overrides = {}, {}
local expectedContext = {overrides={equipped=overrides}}
package.preload['openmw.self']=function()return actor end
package.preload['openmw.input']=function()return {isAltPressed=function()return alt end}end
package.preload['scripts.InventoryExtender.util.constants']=function()return {
    OPT_KEYS={CompareItemsMode='mode'}, COMPARISON_OPTS={ALT='ALT',Always='Always',Never='Never'}
}end
package.preload['scripts.InventoryExtender.util.helpers']=function()return {
    getWindowSettings=function()return {get=function(_,key)assert(key=='mode');return mode end}end,
    isItemEquipped=function(_,a,o)assert(a==actor and o==overrides);return alreadyEquipped end,
    getEquipmentSlots=function()return slots end,
    getEquippedItems=function(s,a,o)
        assert(s==slots and a==actor and o==overrides)
        lookups=lookups+1;return equipped
    end,
}end
package.preload['scripts.InventoryExtender.ui.templates.magic'] = function()
    return {compareItemsTooltip=function(obj, others, icon, ctx)
        comparisons=comparisons+1
        assert(others==equipped and icon and ctx==expectedContext)
        if compareFail then error('comparison provider failure')end
        return {props={},item=obj,comparison=others}
    end, itemTooltip=function(obj, icon, ctx)
        builds=builds+1
        assert(icon and ctx==expectedContext)
        if fail then error('provider failure') end
        return {template=solid,props={},item=obj}
    end}
end
package.path = 'custom/ui-features/InventoryExtenderIntegration/Data_Files/?.lua;'..package.path
local m=require('scripts.InventoryExtender.worldTooltip')
local a={item=true,isValid=function()return true end}
local b={item=true,isValid=function()return true end}
focused=a;m.update(expectedContext,true);assert(builds==1 and calls[#calls][1]==a)
assert(calls[#calls][2].layout.content[1].template==nil, 'opaque inner frame retained')
assert(suppressed==a and calls[#calls][2].layout.content[2].statsFor==a)
for i=1,120 do m.update(expectedContext,true) end
assert(builds==1,'rebuilt unchanged tooltip every frame')
focused=b;m.update(expectedContext,true);assert(builds==2 and calls[#calls][1]==b and destroys==1)
time=.3;m.update(expectedContext,true);assert(builds==3)
focused=nil;m.update(expectedContext,true);assert(calls[#calls][1]==nil and destroys==3)
focused=a;fail=true;m.update(expectedContext,true);assert(calls[#calls][1]==nil)
m.update(expectedContext,true);assert(builds==4,'failed provider retried every frame')
fail=false;time=1;m.update(expectedContext,true);assert(builds==5)
m.update(expectedContext,false);assert(calls[#calls][1]==nil)
m.reset();assert(suppressed==nil);print('PASS: native target reuse, modifier context, throttled refresh, target change, close/disable, failed-provider fallback')


local function update()m.update(expectedContext,true)end
local function tip()return calls[#calls][2].layout.content[1]end
focused=a;mode='ALT';equipped={b};update()
assert(not tip().comparison)
alt=true;update();assert(tip().comparison[1]==b and comparisons==1)
local oldLookups=lookups
for i=1,120 do update()end
assert(lookups==oldLookups and comparisons==1,'comparison scanned/rebuilt every frame')
alt=false;update();assert(not tip().comparison,'ALT release must update immediately')
mode='Always';update();assert(tip().comparison[1]==b)
mode='Never';update();assert(not tip().comparison)
mode='unknown';time=time+.3;update();assert(not tip().comparison)
mode='Always';slots={'ringLeft','ringRight'};equipped={a,b};update()
assert(#tip().comparison==2,'must pass both ring slots to provider')
equipped={};time=time+.3;update();assert(not tip().comparison)
slots=nil;time=time+.3;oldLookups=lookups;update()
assert(not tip().comparison and lookups==oldLookups)
slots={'right'};equipped={b};alreadyEquipped=true;time=time+.3;update()
assert(not tip().comparison)
alreadyEquipped=false;compareFail=true;time=time+.3;update()
assert(not tip().comparison,'comparison error must fall back to single tooltip')
fail=true;time=time+.3;update();assert(calls[#calls][1]==nil and suppressed==nil)
local oldBuilds=builds;update();assert(builds==oldBuilds,'total failure must be throttled')
fail=false;compareFail=false;time=time+.3;update();assert(tip().comparison)
focused={item=false,isValid=function()return true end};update();assert(suppressed==nil)
focused=a;update();m.update(expectedContext,false);assert(suppressed==nil)
focused={item=true,isValid=function()return false end};update();assert(suppressed==nil)
print('PASS: world comparison modes, immediate ALT changes, slot selection, two rings, unsupported/empty/equipped cases, provider fallback, no per-frame equipment scan')

-- Even an older engine exposing the removed resolver must not scan plants.
local plant={item=false,isValid=function()return true end}
ui.getWorldTooltipItem=function()error('removed plant resolver called')end
focused=plant;mode='Never';m.reset();m.update(expectedContext,true)
assert(calls[#calls][1]==nil and suppressed==nil)
focused=a;m.update(expectedContext,true)
assert(calls[#calls][1]==a and calls[#calls][2].layout.content[1].item==a)
print('PASS: unharvested plants use native tooltip without resolution; loose ingredients remain enhanced')
