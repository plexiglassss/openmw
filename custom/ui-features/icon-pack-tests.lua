local paths={
'icons/inventoryextender/base/all.dds',
'icons/inventoryextender/Base/armor.dds',
'icons/inventoryextender/custom/character.png',
'icons/inventoryextender/custom/recent.png',
'icons/inventoryextender/another/all.dds'}
package.preload['openmw.storage']=function()return {playerSection=function()return {get=function()return nil end}end}end
package.preload['openmw.ui']=function()return {}end
package.preload['openmw.vfs']=function()return {pathsWithPrefix=function()local i=0;return function()i=i+1;return paths[i]end end}end
package.path='custom/ui-features/InventoryExtenderIntegration/Data_Files/?.lua;'..package.path
local packs=require('scripts.InventoryExtender.util.iconPack').getAvailablePacks()
assert(#packs==2 and packs[1]=='Base' and packs[2]=='Another')
print('PASS: case-insensitive icon-pack deduplication, valid strict sorting, bridge assets excluded')
