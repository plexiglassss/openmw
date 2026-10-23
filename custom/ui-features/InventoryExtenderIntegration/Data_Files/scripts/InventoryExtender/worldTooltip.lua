-- Reuse the exact IE tooltip layout/providers in the engine's world tooltip.
local ui = require('openmw.ui')
local core = require('openmw.core')
local interfaces = require('openmw.interfaces')
local types = require('openmw.types')
local templates = require('scripts.InventoryExtender.ui.templates.magic')
local current, element, nextRefresh = nil, nil, 0
local M = {}
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
        if element then M.reset() end
        return
    end
    local now = core.getRealTime()
    if object == current and now < nextRefresh then return end
    -- Short refresh covers changing enchant/condition values and modifier state;
    -- targeting changes refresh immediately, without an extra rendering ray.
    M.reset()
    current, nextRefresh = object, now + 0.25
    local ok, layout = pcall(templates.itemTooltip, object, true, ctx)
    if not ok then
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
