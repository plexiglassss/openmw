local core = require('openmw.core')
local ui = require('openmw.ui')
local hasPaperDoll, paperDollType = pcall(function() return ui.TYPE.PaperDoll end)
if not hasPaperDoll then paperDollType = nil end
local auxUi = require('openmw_aux.ui')
local I = require('openmw.interfaces')
local util = require('openmw.util')
local async = require('openmw.async')
local input = require('openmw.input')
local omwself = require('openmw.self')
local v2 = util.vector2

local constants = require('scripts.InventoryExtender.util.constants')
local configPlayer = require('scripts.InventoryExtender.config.player')
local Pickpocket = require('scripts.InventoryExtender.util.pickpocket')

local DragAndDrop = require('scripts.InventoryExtender.ui.dragAndDrop')
local InventoryWindow = require('scripts.InventoryExtender.ui.windows.inventory')
local baseTemplates = require('scripts.InventoryExtender.ui.templates.base')

local validDragModes = {
    Interface = true,
    Container = true,
    Companion = true,
}

local validInventoryModes = {
    Interface = true,
    Barter = true,
    Container = true,
    Companion = true,
}

local function shouldOpenInventorySeparately()
    if paperDollType then return false end
    local windowSettings = configPlayer.window or {}
    -- Separate mode is the default for this patched build.
    if windowSettings.b_OpenInventorySeparatelyV2 == nil then
        return true
    end
    return windowSettings.b_OpenInventorySeparatelyV2 == true
end

local function getSeparateWindowNamesForMode(mode)
    if mode == 'Interface' then
        return { 'Inventory' }
    elseif mode == 'Container' then
        return { 'Inventory', 'Container' }
    elseif mode == 'Barter' then
        return { 'Trade', 'Inventory' }
    elseif mode == 'Companion' then
        return { 'Inventory', 'Companion' }
    end
    return {}
end

local function isInventoryActionPressed()
    -- Keybind-independent: respects the user's OpenMW inventory binding.
    -- Do not hardcode keys here. Other Lua menus also use Interface mode.
    if not input.ACTION or not input.ACTION.Inventory then
        return false
    end
    local ok, pressed = pcall(function()
        return input.isActionPressed(input.ACTION.Inventory)
    end)
    return ok and pressed == true
end

local function shouldMirrorSeparateMode(mode, arg, oldMode)
    if mode ~= 'Interface' then
        return true
    end

    -- If a Lua caller supplied an explicit window list, only mirror when it
    -- explicitly includes Inventory. Empty lists are pure Lua menus.
    if type(arg) == 'table' and type(arg.windows) == 'table' then
        for _, windowName in ipairs(arg.windows) do
            if windowName == 'Inventory' then
                return true
            end
        end
        return false
    end

    -- Interface->Interface refreshes are normally Lua-menu housekeeping, not
    -- vanilla inventory opening.
    if oldMode == 'Interface' then
        return false
    end

    -- Ambiguous Interface mode: mirror only when the actual OpenMW Inventory
    -- action is currently pressed. This fixes Questman, F1 hotkey config
    -- menus, and other mods without modifying them.
    return isInventoryActionPressed()
end

local WindowManager = {}

WindowManager.windows = {
    Inventory = nil,
    Trade = nil,
    Container = nil,
    Companion = nil,
}

WindowManager.ctx = {
    focusedScrollable = nil,
    modalElement = nil,
    activeTooltip = nil,
    -- Set by tooltip-producing mouseMove handlers and consumed by onFrame.
    -- This lets us remove a tooltip as soon as the pointer moves onto a part
    -- of the interface that does not own that tooltip.
    tooltipMouseMoveThisFrame = false,
    modifiers = {
        tooltip = {},
        cellContent = {},
    },
    overrides = {
        equipped = {},
    },
    handlers = {
        onRowUse = {},
        onRowPickup = {},
    },
    isConsoleOpen = false,
    dragAndDrop = DragAndDrop,
    windowArgs = {},
    modeArgs = {},
    lastUiMode = nil,
    stolenItems = {},
    favoriteItems = {},
    favoriteRevision = 0,
    barterState = {
        selling = {},
        buying = {},
        currentMerchantOffer = 0,
        currentBalance = 0,
        totalBalance = 0,
    },
    companionProfit = {},
    pickpocket = nil,
    updateQueue = {},
    barterSystemBlocker = nil,
}

local showing = {
    Inventory = false,
    Trade = false,
    Container = false,
    Companion = false,
}

function WindowManager:create(windowName)
    if windowName == 'Inventory' or windowName == 'Container' or windowName == 'Trade' or windowName == 'Companion' then
        self.windows[windowName] = InventoryWindow:create(windowName, self.ctx)
    end

    return self.windows[windowName]
end

function WindowManager:init()
    for _, windowName in ipairs{"Inventory", "Trade", "Container", "Companion"} do
        self:create(windowName)
    end
end

function WindowManager:reset()
    self:hideBarterSystemBlocker()
    for id, window in pairs(self.windows) do
        if window.element then
            auxUi.deepDestroy(window.element)
        end
        self.windows[id] = nil
    end
end

function WindowManager:refresh()
    for _, window in pairs(self.windows) do
        if window and window:isVisible() then
            window:refresh()
        end
    end
end

function WindowManager:update()
    for _, window in pairs(self.windows) do
        if window then
            if window.updateData then
                window:updateData()
            else
                window:update()
            end
        end
    end
    if self.ctx.activeTooltip then
        auxUi.deepDestroy(self.ctx.activeTooltip)
        self.ctx.activeTooltip = nil
    end
end

function WindowManager:show(windowName, arg)
    if self.ctx.resettingMode then
        return
    end

    if windowName == 'Container' then
        if Pickpocket.isTarget(arg) then
            self.ctx.pickpocket = Pickpocket.createSession(omwself, arg)
        else
            self.ctx.pickpocket = nil
        end
    end

    self.ctx.windowArgs[windowName] = arg or self.ctx.windowArgs[windowName]

    if not self.windows[windowName] then
        self:create(windowName)
    end

    showing[windowName] = true

    self.windows[windowName]:loadState()
    self.windows[windowName]:setVisible(true)
    self.windows[windowName]:updateTarget()

    for name, window in pairs(self.windows) do
        if name ~= windowName and showing[name] then
            window:refresh()
        end
    end
end

function WindowManager:hide(windowName, arg, force)
    if self.ctx.resettingMode then
        return
    end

    showing[windowName] = false
    local window = self.windows[windowName]
    if not window or not window:isVisible() then return end

    window:saveState()

    if not (window:isPinned() and I.UI.getMode() == nil) or force then
        window:setVisible(false)
    end
    if self.ctx.activeTooltip then
        auxUi.deepDestroy(self.ctx.activeTooltip)
        self.ctx.activeTooltip = nil
    end
end

function WindowManager:destroyWindowElement(windowName)
    local window = self.windows[windowName]
    if window and window.element then
        window:saveState()
        auxUi.deepDestroy(window.element)
        window.element = nil
    end
    self.windows[windowName] = nil
end

function WindowManager:recreateWindow(windowName)
    self:destroyWindowElement(windowName)
    self:create(windowName)
end

function WindowManager:showBarterSystemBlocker()
    if self.ctx.barterSystemBlocker then
        return
    end

    -- Same-layer blocker. Create it before recreating/showing IE barter windows
    -- so IE windows sit above it while vanilla/system windows are covered.
    self.ctx.barterSystemBlocker = ui.create {
        layer = 'Windows',
        type = ui.TYPE.Image,
        props = {
            resource = baseTemplates.createTexture('white'),
            color = constants.Colors.BACKGROUND,
            alpha = 0.98,
            relativeSize = v2(1, 1),
        },
        events = {
            mousePress = async:callback(function() return true end),
            mouseRelease = async:callback(function() return true end),
        },
    }
end

function WindowManager:hideBarterSystemBlocker()
    if self.ctx.barterSystemBlocker then
        auxUi.deepDestroy(self.ctx.barterSystemBlocker)
        self.ctx.barterSystemBlocker = nil
    end
end


function WindowManager:onUiModeChanged(oldMode, newMode, arg)
    if not configPlayer.window.b_EnableMod then
        return
    end

    if self.ctx.resettingMode then
        return
    end

    self.ctx.lastUiMode = newMode

    if newMode and arg then
        self.ctx.modeArgs[newMode] = arg
    end

    if shouldOpenInventorySeparately() then
        if newMode == 'Barter' then
            self:showBarterSystemBlocker()
            -- Recreate these after the blocker so they are on top of it.
            self:recreateWindow('Inventory')
            self:recreateWindow('Trade')
        elseif oldMode == 'Barter' then
            self:hideBarterSystemBlocker()
        end
    end

    if shouldOpenInventorySeparately() then
        -- In separate mode we do not register any engine window replacements.
        -- Instead we mirror the currently-open vanilla UI mode with independent Lua windows.
        if oldMode and oldMode ~= newMode and validInventoryModes[oldMode] then
            for _, windowName in ipairs{ 'Inventory', 'Trade', 'Container', 'Companion' } do
                if showing[windowName] then
                    self:hide(windowName, nil, true)
                end
            end
        end

        if newMode and validInventoryModes[newMode] and shouldMirrorSeparateMode(newMode, arg, oldMode) then
            if newMode == 'Container' and arg and Pickpocket.isTarget(arg) then
                self.ctx.pickpocket = Pickpocket.createSession(omwself, arg)
            elseif newMode == 'Container' then
                self.ctx.pickpocket = nil
            end

            if newMode == 'Barter' then
                self.ctx.windowArgs.Trade = arg or self.ctx.windowArgs.Trade
            elseif newMode == 'Container' then
                self.ctx.windowArgs.Container = arg or self.ctx.windowArgs.Container
            elseif newMode == 'Companion' then
                self.ctx.windowArgs.Companion = arg or self.ctx.windowArgs.Companion
            end

            for _, windowName in ipairs(getSeparateWindowNamesForMode(newMode)) do
                self:show(windowName, self.ctx.windowArgs[windowName])
            end
        end
    end

    if oldMode == 'Barter' and self.ctx.barterState.success then
        ui.showMessage(constants.Strings.BARTER_THANK_YOU, {
            showInDialogue = true,
        })
    end

    self.ctx.barterState = {
        selling = {},
        buying = {},
        currentMerchantOffer = 0,
        currentBalance = 0,
    }

    if newMode == nil and oldMode == 'Container' and self.ctx.windowArgs.Container then
        if self.ctx.pickpocket and self.ctx.pickpocket.active and not self.ctx.pickpocket.resolved and
           Pickpocket.isTarget(self.ctx.windowArgs.Container) then
            local success = Pickpocket.rollClose(omwself, self.ctx.windowArgs.Container)
            if not success then
                core.sendGlobalEvent('IE_CommitPickpocket', {
                    player = omwself,
                    target = self.ctx.windowArgs.Container,
                    victimAware = true,
                })
            end
        end
        self.ctx.pickpocket = nil
        self.ctx.windowArgs.Container:sendEvent('IE_ContainerClosed')
    end

    if self.ctx.modalElement then
        auxUi.deepDestroy(self.ctx.modalElement)
        self.ctx.modalElement = nil
    end
    if self.ctx.activeTooltip then
        auxUi.deepDestroy(self.ctx.activeTooltip)
        self.ctx.activeTooltip = nil
    end
    if self.ctx.dragAndDrop.wrapper then
        self.ctx.dragAndDrop:setWrapperEnabled(not configPlayer.misc.b_TooltipCompatibilityMode and validDragModes[newMode])
        self.ctx.dragAndDrop.lastHoveredObject = nil
    end
    self.ctx.dragAndDrop:stopDrag()

    for windowName, window in pairs(self.windows) do
        window:loadState()
        if not window:isPinnable() and window:isVisible() and not showing[windowName] and newMode ~= nil then
            window:setVisible(false)
        elseif window:isPinnable() and window:isPinned() and not window:isVisible() and newMode == nil then
            window:setVisible(true)
            window:updateData()
        end
    end

    if newMode and newMode ~= 'Interface' and validInventoryModes[newMode] and shouldMirrorSeparateMode(newMode, arg, oldMode) and self.windows.Inventory then
        self.windows.Inventory:setFocused(true)
    end
end

function WindowManager:onMouseWheel(v, h)
    if self.ctx.focusedScrollable and self.ctx.focusedScrollable.layout then
        local layout = self.ctx.focusedScrollable.layout
        local pos = layout.content[1].props.position
        layout.content[1].props.position = util.vector2(
            pos.x,
            util.clamp(pos.y + v * layout.userData.scrollStep, -layout.userData.scrollLimit, 0)
        )
        layout.userData.onScroll()
        return true
    end

    -- If the mouse is over an Inventory Extender window, consume the wheel
    -- even when the exact child under the cursor is not a scrollable widget.
    -- Otherwise the same wheel event can also scroll vanilla/system or other
    -- mod windows underneath.
    for _, window in pairs(self.windows) do
        if window and window:isVisible() and window:isFocused() then
            return true
        end
    end

    return false
end

function WindowManager:onFrame()
    if not configPlayer.window.b_EnableMod then return end

    if self.ctx.resettingModeProcessed then
        self.ctx.resettingModeProcessed = false
        self.ctx.resettingMode = false
        return
    end

    if self.ctx.resettingMode then
        self.ctx.resettingModeProcessed = true
    end

    if self.ctx.focusedInteractiveDelayed ~= nil then
        if self.ctx.focusedInteractiveDelayed == false then
            self.ctx.focusedInteractive = nil
        else
            self.ctx.focusedInteractive = self.ctx.focusedInteractiveDelayed
        end
        self.ctx.focusedInteractiveDelayed = nil
    end

    for element in pairs(self.ctx.updateQueue) do
        element:update()
    end
    self.ctx.updateQueue = {}

    local dt = core.getRealFrameDuration()

    -- I.Cursor when??? (:
    local mouseMoved
    if I.UI.getMode() ~= nil then
        if input.getMouseMoveX() ~= 0 or input.getMouseMoveY() ~= 0 then
            mouseMoved = true
        end
    end

    -- Lua UI focusLoss is not guaranteed when the pointer leaves an
    -- Inventory Extender element for a vanilla MWUI window (map, paper doll,
    -- stats, etc.).  In that case the last item tooltip used to remain visible
    -- indefinitely.  Tooltip owners mark each mouse-move frame below; if the
    -- pointer moved but no tooltip owner saw it, the old tooltip is stale.
    local tooltipMouseMoveThisFrame = self.ctx.tooltipMouseMoveThisFrame == true
    self.ctx.tooltipMouseMoveThisFrame = false
    if mouseMoved and self.ctx.activeTooltip and not tooltipMouseMoveThisFrame then
        if self.ctx.activeTooltip.layout then
            auxUi.deepDestroy(self.ctx.activeTooltip)
        end
        self.ctx.activeTooltip = nil
    end

    for _, window in pairs(self.windows) do
        -- Clear stale hovered row pos if mouse moved NOT over the element's item table
        if mouseMoved then
            if window and window.itemTable and window.itemTable.layout and window.itemTable.layout.userData.getState then
                local state = window.itemTable.layout.userData.getState()
                local hadMouseMoveThisFrame = state.hadMouseMoveThisFrame
                state.hadMouseMoveThisFrame = false

                if not hadMouseMoveThisFrame and state.lastPointerRowPos then
                    state.lastPointerRowPos = nil
                    state.isPointerOverContent = false
                end
            end

            if window.element and not window.element.layout.userData.hadMouseMoveThisFrame then
                window:setFocused(false)
            end
            window.element.layout.userData.hadMouseMoveThisFrame = false
        end

        if window and window.element and window.element.layout.userData then
            local userData = window.element.layout.userData
            local focusDelayed = userData.focusDelayed
            if focusDelayed ~= nil then
                if not focusDelayed then
                    if self.ctx.cursorAttachedIcon then
                        self.ctx.cursorAttachedIcon.layout.props.visible = false
                        self.ctx.updateQueue[self.ctx.cursorAttachedIcon] = true
                    end
                end
                if focusDelayed ~= userData.focused then
                    userData.focused = focusDelayed
                end
                userData.focusDelayed = nil
            end
        end
    end

    if self.ctx.focusedScrollable and self.ctx.focusedScrollable.layout then
        local rightStick = input.getAxisValue(input.CONTROLLER_AXIS.RightY)
        if math.abs(rightStick) > 0.2 then
            local layout = self.ctx.focusedScrollable.layout
            local pos = layout.content[1].props.position
            layout.content[1].props.position = util.vector2(
                pos.x,
                util.clamp(pos.y - rightStick * layout.userData.scrollStep / 4 * dt * 60, -layout.userData.scrollLimit, 0)
            )
            layout.userData.onScroll()
        end
    end
end

function WindowManager:onControllerButtonPress(id)
    if not configPlayer.window.b_EnableMod then return end
    if id == input.CONTROLLER_BUTTON.Y and self.ctx.modalElement then
        auxUi.deepDestroy(self.ctx.modalElement)
        self.ctx.modalElement = nil
        return
    end

    for _, window in pairs(self.windows) do
        if window:isFocused() then
            window:onControllerButtonPress(id)
        end
    end

    if validDragModes[I.UI.getMode() or ''] then
        WindowManager.ctx.dragAndDrop:onControllerButtonPress(id)
    end
end

function WindowManager:onControllerButtonRelease(id)
    if not configPlayer.window.b_EnableMod then return end

    if validDragModes[I.UI.getMode() or ''] then
        WindowManager.ctx.dragAndDrop:onControllerButtonRelease(id)
    end
end

function WindowManager:onMouseButtonPress(button)
    if not configPlayer.window.b_EnableMod then return end

    if validDragModes[I.UI.getMode() or ''] then
        WindowManager.ctx.dragAndDrop:onMouseButtonPress(button)
    end
end

function WindowManager:onMouseButtonRelease(button)
    if not configPlayer.window.b_EnableMod then return end

    if validDragModes[I.UI.getMode() or ''] then
        WindowManager.ctx.dragAndDrop:onMouseButtonRelease(button)
    end
end

function WindowManager:onKeyPress(key)
    if not configPlayer.window.b_EnableMod then return end

    local focusedInteractive = WindowManager.ctx.focusedInteractive
    local focusedWindow = nil
    for _, window in pairs(self.windows) do
        if window and window:isVisible() and window:isFocused() then
            focusedWindow = window
            break
        end
    end

    if key.code == input.KEY.A or key.code == input.KEY.D then
        if focusedWindow and focusedWindow.categoryFilter then
            local direction = key.code == input.KEY.A and input.CONTROLLER_BUTTON.LeftShoulder or input.CONTROLLER_BUTTON.RightShoulder
            focusedWindow:onControllerButtonPress(direction)
            return true
        end
    end

    if key.code == (configPlayer.keybinds.k_ToggleFavorite or input.KEY.F) then
        if not focusedInteractive or not focusedInteractive.layout or not focusedInteractive.layout.userData then
            return
        end

        if focusedInteractive.layout.userData.onFavoriteToggle then
            return focusedInteractive.layout.userData.onFavoriteToggle()
        end
        return
    end

    if key.code ~= configPlayer.keybinds.k_UseItem then return end

    if configPlayer.keybinds.b_SwapUsePickup and WindowManager.ctx.dragAndDrop.draggingObject then
        if focusedWindow and focusedWindow.target then
            local state = focusedWindow.itemTable and focusedWindow.itemTable.layout and focusedWindow.itemTable.layout.userData.getState and focusedWindow.itemTable.layout.userData.getState()
            if state and state.lastPointerRowPos ~= nil then
                state.lastUsedRowPos = state.lastPointerRowPos
            end
            WindowManager.ctx.dragAndDrop:stopDrag(focusedWindow.target)
            return true
        end
    end

    if not focusedInteractive or not focusedInteractive.layout or not focusedInteractive.layout.userData then
        return
    end

    if configPlayer.keybinds.b_SwapUsePickup then
        if focusedInteractive.layout.userData.onKBMRowUse then
            focusedInteractive.layout.userData.onKBMRowUse()
            return true
        elseif focusedInteractive.layout.userData.onRowUse then
            focusedInteractive.layout.userData.onRowUse()
            return true
        end
    elseif focusedInteractive.layout.userData.onKBMRowPickup then
        focusedInteractive.layout.userData.onKBMRowPickup()
        return true
    elseif focusedInteractive.layout.userData.onRowPickup then
        focusedInteractive.layout.userData.onRowPickup()
        return true
    end
end

WindowManager.ctx.dragAndDrop:init(WindowManager.ctx)

input.registerTriggerHandler('Activate', async:callback(function()
    for _, button in pairs(input.CONTROLLER_BUTTON) do
        if input.isControllerButtonPressed(button) then
            return
        end
    end

    local mode = I.UI.getMode()
    if mode == 'Container' or mode == 'Barter' or mode == 'Companion' then
        I.UI.removeMode(mode)
        return false
    end
    return false
end))

return WindowManager
