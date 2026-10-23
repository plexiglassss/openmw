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
local expectedContext = {}
package.preload['scripts.InventoryExtender.ui.templates.magic'] = function()
    return {itemTooltip=function(obj, icon, ctx)
        builds=builds+1
        assert(icon and ctx==expectedContext)
        if fail then error('provider failure') end
        return {props={},item=obj}
    end}
end
package.path = 'custom/ui-features/InventoryExtenderIntegration/Data_Files/?.lua;'..package.path
local m=require('scripts.InventoryExtender.worldTooltip')
local a={item=true,isValid=function()return true end}
local b={item=true,isValid=function()return true end}
focused=a;m.update(expectedContext,true);assert(builds==1 and calls[#calls][1]==a)
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
