-- Reuse the exact IE tooltip layout/providers in the engine's world tooltip.
local ui = require('openmw.ui')
local core = require('openmw.core')
local interfaces = require('openmw.interfaces')
local types = require('openmw.types')
local input = require('openmw.input')
local omwself = require('openmw.self')
local helpers = require('scripts.InventoryExtender.util.helpers')
local constants = require('scripts.InventoryExtender.util.constants')
local templates = require('scripts.InventoryExtender.ui.templates.magic')
local current, element, nextRefresh = nil, nil, 0
local comparing = false
local M = {}
local function wantsComparison()
    local mode = helpers.getWindowSettings():get(constants.OPT_KEYS.CompareItemsMode)
    return mode == constants.COMPARISON_OPTS.Always
        or (mode == constants.COMPARISON_OPTS.ALT and input.isAltPressed())
end
local function comparisonTooltip(object, ctx)
    local overrides = ctx.overrides and ctx.overrides.equipped
    if helpers.isItemEquipped(object, omwself, overrides) then return nil end
    local slots = helpers.getEquipmentSlots(object)
    if not slots then return nil end
    local equipped = helpers.getEquippedItems(slots, omwself, overrides)
    if not equipped or #equipped == 0 then return nil end
    return templates.compareItemsTooltip(object, equipped, true, ctx)
end
function M.reset()
    if interfaces.ObjectInsightTooltip then interfaces.ObjectInsightTooltip.setNativeTooltipObject(nil) end
    if ui.setWorldTooltip then ui.setWorldTooltip(nil, nil) end
    if element and element.layout then element:destroy() end
    current, element, nextRefresh = nil, nil, 0
end
function M.update(ctx, enabled)
    if not ui.getWorldTooltipObject then return end
    local object = enabled and ui.getWorldTooltipObject() or nil
    if not object or not object:isValid() or not types.Item.objectIsInstance(object) then
        if current or element then M.reset() end
        return
    end
    local now = core.getRealTime()
    local modeOK, compare = pcall(wantsComparison)
    compare = modeOK and compare or false
    if object == current and compare == comparing and now < nextRefresh then return end
    -- Short refresh covers changing enchant/condition values and modifier state;
    -- targeting and ALT changes refresh immediately, without an extra rendering ray.
    M.reset()
    current, nextRefresh = object, now + 0.25
    comparing = compare
    local ok, layout
    if compare then
        ok, layout = pcall(comparisonTooltip, object, ctx)
    end
    -- A comparison provider must never hide the ordinary item tooltip.
    if not ok or not layout then
        ok, layout = pcall(templates.itemTooltip, object, true, ctx)
    end
    if not ok or not layout then
        print('[InventoryExtender] World tooltip failed: ' .. tostring(layout))
        return -- native tooltip remains available
    end
    local insight = interfaces.ObjectInsightTooltip
    if insight then
        local success, extra = pcall(insight.nativeSupplement, object)
        if success and extra then
            layout = {type=ui.TYPE.Flex, content=ui.content({layout, extra})}
        end
    end
    layout.layer = '' -- engine attaches this element inside its normal tooltip
    layout.props = layout.props or {}
    element = ui.create(layout)
    ui.setWorldTooltip(object, element)
    if insight then insight.setNativeTooltipObject(object) end
end
return M
