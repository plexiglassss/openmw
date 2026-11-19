local core = require('openmw.core')
local self = require('openmw.self')
local async = require('openmw.async')
local time = require('openmw_aux.time')
local ui = require('openmw.ui')
local input = require('openmw.input')
local I = require('openmw.interfaces')
local util = require('openmw.util')
local storage = require('openmw.storage')
local types = require("openmw.types")
local debug = require('openmw.debug')
local ambient = require('openmw.ambient')
local animation = require("openmw.animation")
local nearby = require("openmw.nearby")

local include = require("scripts.advanced_world_map.utils.include")

local pDoor = require("scripts.advanced_world_map.helpers.protectedDoor")

local log = require("scripts.advanced_world_map.utils.log")

local commonData = require("scripts.advanced_world_map.common")

local configLib = require("scripts.advanced_world_map.config.configLib")
local config = require("scripts.advanced_world_map.config.config")
local tableLib = require("scripts.advanced_world_map.utils.table")
local dateLib = require("scripts.advanced_world_map.utils.date")
local cellLib = require("scripts.advanced_world_map.utils.cell")

local localStorage = require("scripts.advanced_world_map.storage.localStorage")
local playerPos = require("scripts.advanced_world_map.playerPosition")

local realTimer = require("scripts.advanced_world_map.realTimer")

local menuMode = require("scripts.advanced_world_map.ui.menuMode")

local eventSys = require("scripts.advanced_world_map.eventSys")
local mapTextureHandler = require("scripts.advanced_world_map.mapTextureHandler")
local menuHandler = require("scripts.advanced_world_map.menuHandler")
local mapDataHandler = require("scripts.advanced_world_map.mapDataHandler")
local discoveredLocs = require("scripts.advanced_world_map.discoveredLocations")
local disabledDoors = require("scripts.advanced_world_map.disabledDoors")
local disabledActors = require("scripts.advanced_world_map.disabledActors")
local northMarker = require("scripts.advanced_world_map.northMarker")
local keysModule = require("scripts.advanced_world_map.input.keys")
local hotkeyLayers = require("scripts.advanced_world_map.input.hotkeyLayers")

local mapMenu = require("scripts.advanced_world_map.ui.menu.map")
local firstInitMenu = require("scripts.advanced_world_map.ui.menu.firstInit")
local gamepadHotkeyInfoMenu = require("scripts.advanced_world_map.ui.menu.gamepadHotkeyInfo")

local messageBox = require("scripts.advanced_world_map.ui.menu.messageBox")

local markers = require("scripts.advanced_world_map.widgets.markers")
local notesWidgetData = require("scripts.advanced_world_map.widgets.notes.data")

-- widgets
require("scripts.advanced_world_map.widgets.mapTypeLabel")
require("scripts.advanced_world_map.widgets.markers")
require("scripts.advanced_world_map.widgets.legend")
require("scripts.advanced_world_map.widgets.search")
require("scripts.advanced_world_map.widgets.transport")
local fastTravel = require("scripts.advanced_world_map.widgets.fastTravel")
require("scripts.advanced_world_map.widgets.notes.note")
local cellNameWidget = require("scripts.advanced_world_map.widgets.cellName")

include("scripts.advanced_world_map.fixes.TotSP")

local l10n = core.l10n(commonData.l10nKey)

local hasAttemptedToGetData = false
local lastUiModeTimestamp = 0
local lastUiModeId = ""

local registeredAsDefault = false
local registerAsDefaultMap
local openMenu
local discoverNearby
local finishDoorQuery
local pendingDoors


pcall(function ()
    if not ui.layers.indexOf(commonData.messageLayer) then
        ui.layers.insertBefore("DragAndDrop", commonData.messageLayer, { interactive = true })
    end
    if not ui.layers.indexOf(commonData.HUDLayer) then
        ui.layers.insertAfter("HUD", commonData.HUDLayer, { interactive = false })
    end
end)


local fightingActors = {}


storage.playerSection(commonData.configTilesetSectionName):subscribe(async:callback(function(_, id)
    mapMenu.clearMapWidgetCache()
end))

storage.playerSection(commonData.configLegendSectionName):subscribe(async:callback(function(_, id)
    mapMenu.clearMapWidgetCache()
end))

local isDataStorageTimerRunning
local configDataSection = storage.playerSection(commonData.configDataSectionName)
configDataSection:subscribe(async:callback(function(_, id)
    mapTextureHandler.init()
    mapMenu.clearMapWidgetCache()
    local isInitializerTrigger =  id == "data.initializer"
    local isForceUpdateTrigger = id == "data.forceUpdateData"
    if isInitializerTrigger or isForceUpdateTrigger then
        if isInitializerTrigger and configDataSection:get(id) == commonData.dataInitializerTypes[6] or
                isForceUpdateTrigger and configDataSection:get(id) == true then
            if not isDataStorageTimerRunning then
                isDataStorageTimerRunning = true
                async:newUnsavableSimulationTimer(0.1, function ()
                    if isDataStorageTimerRunning then
                        if config.data.data.forceUpdateData then
                            configLib.setValue("data.forceUpdateData", false)
                        end
                        core.sendGlobalEvent("AdvWMap:initMapData", {plRef = self.object, options = {force = true}})
                        isDataStorageTimerRunning = false
                    end
                end)
            end
        else
            isDataStorageTimerRunning = false
        end
    end
end))

storage.playerSection(commonData.configNotesSectionName):subscribe(async:callback(function(_, id)
    mapMenu.clearMapWidgetCache()
end))

storage.playerSection(commonData.configUISectionName):subscribe(async:callback(function(_, id)
    if id == "ui.defaultTextureColor" then
        mapTextureHandler.init()
    end
    mapMenu.clearMapWidgetCache()
end))

local mainSectionStorage = storage.playerSection(commonData.configMainSectionName)

local function resetSizePos()
    menuHandler.destroyAllMenus()
    configLib.setValue("main.relativeSize.x", config.default.main.relativeSize.x)
    configLib.setValue("main.relativeSize.y", config.default.main.relativeSize.y)
    configLib.setValue("main.relativePosition.x", config.default.main.relativePosition.x)
    configLib.setValue("main.relativePosition.y", config.default.main.relativePosition.y)
    configLib.setValue("main.minimap.relativeSize.x", config.default.main.minimap.relativeSize.x)
    configLib.setValue("main.minimap.relativeSize.y", config.default.main.minimap.relativeSize.y)
    configLib.setValue("main.minimap.relativePosition.x", config.default.main.minimap.relativePosition.x)
    configLib.setValue("main.minimap.relativePosition.y", config.default.main.minimap.relativePosition.y)
    configLib.setValue("main.charMenu.relativeSize.x", config.default.main.charMenu.relativeSize.x)
    configLib.setValue("main.charMenu.relativeSize.y", config.default.main.charMenu.relativeSize.y)
    configLib.setValue("main.charMenu.relativePosition.x", config.default.main.charMenu.relativePosition.x)
    configLib.setValue("main.charMenu.relativePosition.y", config.default.main.charMenu.relativePosition.y)
    ui.showMessage(l10n("MapSizePositionResetMessage"))
end

if mainSectionStorage:get("main.resetSizePos") then
    resetSizePos()
    mainSectionStorage:set("main.resetSizePos", false)
end

local isStorageTimerRunning = false
mainSectionStorage:subscribe(async:callback(function(_, _)
    local reset = mainSectionStorage:get("main.resetSizePos")
    if reset and not isStorageTimerRunning then
        isStorageTimerRunning = true
        async:newUnsavableSimulationTimer(0.01, function ()
            resetSizePos()
            mainSectionStorage:set("main.resetSizePos", false)
            isStorageTimerRunning = false
        end)
    end
end))


local defaultSectionStorage = storage.playerSection(commonData.configMiscSectionName)

if defaultSectionStorage:get("input.validateMHotkey") == true then
    async:newUnsavableSimulationTimer(1, function ()
        defaultSectionStorage:set("input.validateMHotkey", nil)

        if config.data.main.menuKey ~= "N" then return end

        local bindingSection = storage.playerSection("OMWInputBindings")
        local binds = bindingSection:asTable()
        local isMKeyAvailable = true
        for kId, dt in pairs(binds) do
            if dt.device == "keyboard" and dt.type == "trigger" and dt.button == input.KEY.M and dt.key then
                if input.triggers[dt.key] then
                    isMKeyAvailable = false
                    break
                end
            end
        end

        if isMKeyAvailable then
            configLib.setValue("main.menuKey", "M")
        end
    end)
end


local function onInit()
    if pendingDoors then pendingDoors.job:cancel(); pendingDoors = nil end
    if not localStorage.isPlayerStorageReady() then
        localStorage.initPlayerStorage()
    end
    playerPos.init()
    discoveredLocs.init()
    disabledDoors.init()
    disabledActors.init()
    -- must be after localStorage init
    notesWidgetData.loadData()
    if core.API_REVISION >= 129 then
        localStorage.data[commonData.disableDialogueMarkersFieldId] = true
    end
    core.sendGlobalEvent("AdvWMap:requestTimeUpdate", self.object)
end


local function onLoad(data)
    if pendingDoors then pendingDoors.job:cancel(); pendingDoors = nil end
    localStorage.initPlayerStorage(data)
    playerPos.init()
    discoveredLocs.init()
    disabledDoors.init()
    disabledActors.init()
    if config.data.data.hasSafeInitMessageBeenShown then
        if config.data.data.autoDataUpdate or config.data.data.forceUpdateData then
            hasAttemptedToGetData = true
            local force = config.data.data.forceUpdateData
            if config.data.data.forceUpdateData then
                configLib.setValue("data.forceUpdateData", false)
            end
            core.sendGlobalEvent("AdvWMap:initMapData", {plRef = self.object, options = {force = force}})
        else
            core.sendGlobalEvent("AdvWMap:initMapData", {plRef = self.object, options = {skipRebuild = true}})
        end
    end
    -- must be after localStorage init
    notesWidgetData.loadData()
    core.sendGlobalEvent("AdvWMap:requestTimeUpdate", self.object)

    if (config.data.main.minimap.enabled and localStorage.data[commonData.inMinimapModeKeyId] == true or
            localStorage.data[commonData.pinnedStateFieldId]) and
            not (configLib.data.main.firstInitMenu or not configLib.data.data.hasSafeInitMessageBeenShown or
                configLib.data.message.firstInitMenuShown < configLib.data.message.firstInitMenuShownCurrent) then
        async:newUnsavableSimulationTimer(1, function ()
            if (localStorage.data[commonData.inMinimapModeKeyId] == true or localStorage.data[commonData.pinnedStateFieldId]) and
                    not menuHandler.getMenu(commonData.mapMenuId) then
                openMenu(false, true, config.data.main.overrideDefault)
            end
        end)
    end
end


local function onSave()
    if pendingDoors then finishDoorQuery(true) end
    local data = {}
    localStorage.save(data)
    notesWidgetData.saveData()
    return data
end


local function onMouseWheel(vertical)
    local old = vertical
    vertical = util.clamp(vertical, -6, 6)
    menuHandler.onMouseWheelCallback(vertical)
end


local function onCombatTargetsChanged(eventData)
    pcall(function ()
        if eventData.actor == nil then return end

        if next(eventData.targets) ~= nil then
            fightingActors[eventData.actor.id] = true
        else
            fightingActors[eventData.actor.id] = nil
        end
    end)
end


local function getMenu()
    return menuHandler.getMenu(commonData.mapMenuId)
end


local function initDataForMenu(options)
    local initialized = mapDataHandler.isInitialized()

    if not initialized then
        if not hasAttemptedToGetData then
            hasAttemptedToGetData = true
            if menuMode.isMenuInteractive() then
                menuMode.deactivate()
            end
            options = options or {}
            options.skipRebuild = not config.data.data.autoDataUpdate
            core.sendGlobalEvent("AdvWMap:initMapData", {plRef = self.object, options = options})
            return false
        else
            ui.showMessage(l10n("mapDataNotInitialized"))
            return false
        end
    end
    hasAttemptedToGetData = true
    discoveredLocs.updatePending()
    return true
end


local function isShouldShowFirstInit()
    return configLib.data.main.firstInitMenu or not configLib.data.data.hasSafeInitMessageBeenShown or
        configLib.data.message.firstInitMenuShown < configLib.data.message.firstInitMenuShownCurrent
end

local function isShouldReplacePrevous()
    return (core.getRealTime() - lastUiModeTimestamp) < 1 and #I.UI.modes == 1 and not menuMode.essentialModes[lastUiModeId] and
        not menuMode.isActivated()
end

local function tryShowFirstInit(createMenuFunc, params)
    local replacePreviousMode = isShouldReplacePrevous()

    if isShouldShowFirstInit() then
        menuHandler.destroyAllMenus()

        if replacePreviousMode and lastUiModeId == menuMode.modeId and menuMode.isModeActive(lastUiModeId) then
            I.UI.removeMode(lastUiModeId)
        end
        if not menuMode.isMenuInteractive() then
            menuMode.activate()
        end
        if replacePreviousMode and lastUiModeId ~= menuMode.modeId and menuMode.isModeActive(lastUiModeId) then
            I.UI.removeMode(lastUiModeId)
        end

        configLib.setValue("message.firstInitMenuShown", configLib.data.message.firstInitMenuShownCurrent)

        menuHandler.registerMenu(commonData.firstInitMenuId, firstInitMenu.new{
            yesCallback = function (fiMenu)
                if not configLib.data.data.hasSafeInitMessageBeenShown then
                    configLib.setValue("data.hasSafeInitMessageBeenShown", true)
                end
                configLib.setValue("main.firstInitMenu", false)
                if fiMenu.settings.overrideDefault and not registeredAsDefault then
                    registerAsDefaultMap()
                    configLib.setValue("main.overrideDefault", true)
                    -- menuMode.deactivate()
                else
                    createMenuFunc(params and table.unpack(params) or nil)
                end
            end
        })
    else
        return true
    end
end


openMenu = function (inMenuMode, internal, hideCloseBtn, params)
    if not tryShowFirstInit(openMenu, {inMenuMode, internal, hideCloseBtn, params}) then
        return
    end
    if not initDataForMenu({openMenu = true, openInMenuMode = inMenuMode}) then return end

    params = params or {}

    if inMenuMode and not menuMode.isMenuInteractive() then
        menuMode.activate()
    end

    local menu = menuHandler.getMenu(commonData.mapMenuId)
    if menu then
        if not internal then
            menu:externalActivate()
        end
        return menu
    end

    menuHandler.registerMenu(commonData.mapMenuId, mapMenu.create{
        isCreatedExternally = not internal,
        hideCloseBtn = hideCloseBtn,
        relativePosition = params.relativePosition,
        relativeSize = params.relativeSize,
        inCharacterMenu = params.inCharacterMenu,
        onClose = function ()
            if inMenuMode then
                menuMode.deactivate()
            end
        end
    })
    gamepadHotkeyInfoMenu.create(true)

    local menu = menuHandler.getMenu(commonData.mapMenuId)
    if menu and menu:updateInteractiveElements{visible = not internal and true or nil} then
        menu:update()
    end
    return menu
end


local function closeMenu()
    menuHandler.destroyAllMenus()
end


registerAsDefaultMap = function ()
    registeredAsDefault = true
    I.UI.registerWindow("Map",
        function()
            realTimer.newTimer(0.05, function ()
                openMenu(nil, true, true, {
                    relativeSize = util.vector2(config.data.main.charMenu.relativeSize.x, config.data.main.charMenu.relativeSize.y),
                    relativePosition = util.vector2(config.data.main.charMenu.relativePosition.x, config.data.main.charMenu.relativePosition.y),
                    inCharacterMenu = true
                })
            end)
        end,
        function ()
            if localStorage.data[commonData.pinnedStateFieldId] then
                local menu = menuHandler.getMenu(commonData.mapMenuId)
                if menu then
                    if menu:updateInteractiveElements() then
                        menu:update()
                    end
                    return
                end
            end

            closeMenu()
        end
    )
end


if configLib.data.main.overrideDefault then
    registerAsDefaultMap()
end


local function toggleMenu()
    local timestamp = core.getRealTime()

    local menu = menuHandler.getMenu(commonData.mapMenuId)
    local isInCharacterMenu = I.UI.getMode() == "Interface"
    local istInCharacterMenuMode = menu and menu.isInCharacterMenuMode and isInCharacterMenu and config.data.main.overrideDefault and
        (not I.GamepadControls or not I.GamepadControls.isControllerMenusEnabled())
    local isHiddenInMinimapMode = menu and not menu.isInActiveMode
    local isRegularMode = menu and not menu.isInCharacterMenuMode and menu.isInActiveMode and not config.data.main.overrideDefault
    local replacePreviousMode = isShouldReplacePrevous()

    if menu and not istInCharacterMenuMode and not isHiddenInMinimapMode and not (isInCharacterMenu and not istInCharacterMenuMode) and
            not replacePreviousMode or isRegularMode then
        if menuMode.isActive() then
            if localStorage.data[commonData.pinnedStateFieldId] then
                menuMode.deactivate()
            else
                menuHandler.destroyMenu(commonData.mapMenuId)
            end
        else
            if localStorage.data[commonData.pinnedStateFieldId] and not menuMode.isMenuInteractive() then
                menuMode.activate()
            else
                menuHandler.destroyMenu(commonData.mapMenuId)
            end
        end
    else
        local function registerMenu(useCharacterMenuParams)
            if not initDataForMenu({toggleMenu = true}) then return end

            discoverNearby()

            if not menuMode.isMenuInteractive() then
                menuMode.activate()
            elseif replacePreviousMode then
                if lastUiModeId == menuMode.modeId and menuMode.isModeActive(lastUiModeId) then
                    I.UI.removeMode(lastUiModeId)
                end
                menuMode.activate()
                if lastUiModeId ~= menuMode.modeId and menuMode.isModeActive(lastUiModeId) then
                    I.UI.removeMode(lastUiModeId)
                end
            end

            local relPos, relSize
            if useCharacterMenuParams then
                relSize = util.vector2(config.data.main.charMenu.relativeSize.x, config.data.main.charMenu.relativeSize.y)
                relPos = util.vector2(config.data.main.charMenu.relativePosition.x, config.data.main.charMenu.relativePosition.y)
            end

            menuHandler.registerMenu(commonData.mapMenuId, mapMenu.create{
                relativePosition = relPos,
                relativeSize = relSize,
                inCharacterMenu = useCharacterMenuParams,
                hideCloseBtn = useCharacterMenuParams,
                onClose = function ()
                    menuMode.deactivate()
                end
            })
            gamepadHotkeyInfoMenu.create(false)
        end

        if not menuMode.isMenuInteractive() then
            menuMode.activate()
        elseif replacePreviousMode and #I.UI.modes == 1 then
            if menuMode.isModeActive(lastUiModeId) then
                I.UI.removeMode(lastUiModeId)
            end
            menuMode.activate()
        end

        if tryShowFirstInit(registerMenu) then
            local useCharacterMenuParams = isInCharacterMenu and config.data.main.overrideDefault
            if menu and (istInCharacterMenuMode or isHiddenInMinimapMode) then
                menuHandler.destroyMenu(commonData.mapMenuId)
                useCharacterMenuParams = false
            end
            registerMenu(useCharacterMenuParams)
        end
    end
end

if I.DijectKeyBindings then
    I.DijectKeyBindings.action.register(commonData.menuKeyId, function ()
        core.sendGlobalEvent("AdvWMap:toggleMenuCheck", self.object)
    end)
    I.DijectKeyBindings.action.register(commonData.menuKeyAltId, function ()
        core.sendGlobalEvent("AdvWMap:toggleMenuCheck", self.object)
    end)
end


local function updateTimer()
    playerPos.checkPos()

    realTimer.newTimer(0.2, function ()
        updateTimer()
    end)
end

updateTimer()


local nearbyDoors = {}
local function addNearbyDoors()
    if pendingDoors then finishDoorQuery(true) end
    nearbyDoors = {}
    for _, ref in pairs(nearby.doors) do
        -- additional check for isTeleport, since nearby.doors may sometimes include non-doors
        if types.Door.objectIsInstance(ref) and types.Door.isTeleport(ref) then
            table.insert(nearbyDoors, ref)
        end
    end
end

async:newUnsavableSimulationTimer(0.25, function ()
    addNearbyDoors()
end)


local function discoverDoor(ref)
    if not ref or not ref:isValid() or not types.Door.isTeleport(ref) then return end
    local cell = pDoor.destCell(ref)
    if cell and not discoveredLocs.isDiscovered(cell.id) then
        local newDiscovered = discoveredLocs.addDiscoveredCell(cell)
        if newDiscovered then markers.updateDiscovered(newDiscovered) end
    end
end

local function applyDistances(distances)
    for i = 1, #distances do discoverDoor(distances:object(i)) end
end

finishDoorQuery = function(force)
    local p = pendingDoors
    if not p then return end
    if not self.cell or p.cell ~= self.cell.id then
        p.job:cancel()
        pendingDoors = nil
        return
    end
    local status, result = p.job:poll()
    if status == "pending" and not force then return end
    pendingDoors = nil
    if status ~= "ready" then
        p.job:cancel()
        result = nearby.queryDistances(p.snapshot, p.origin, p.options)
    end
    applyDistances(result)
end

discoverNearby = function(defer)
    if pendingDoors then finishDoorQuery(true) end
    local radius = configLib.data.main.discoveryRadius
    if radius == 0 then return end
    local options = {maxDistance = radius}
    local origin = self.position
    if defer and nearby.queryDistancesAsync and #nearbyDoors >= 24 and #nearbyDoors <= 16384 and self.cell then
        local snapshot = nearby.snapshotPositions(nearbyDoors)
        local job = nearby.queryDistancesAsync(snapshot, origin, options)
        if job then
            pendingDoors = {job = job, snapshot = snapshot, origin = origin, options = options, cell = self.cell.id}
        else
            applyDistances(nearby.queryDistances(snapshot, origin, options))
        end
    elseif nearby.queryDistances then
        applyDistances(nearby.queryDistances(nearbyDoors, origin, options))
    else
        for _, ref in pairs(nearbyDoors) do
            if types.Door.isTeleport(ref) and (ref.position - origin):length() <= radius then discoverDoor(ref) end
        end
    end

    local newPositions = discoveredLocs.discoverPosition(self.cell, self.position)
    if newPositions and mapMenu.activeMenuMeta then
        mapMenu.updateFog(self.cell, newPositions)
    end
end

time.runRepeatedly(function() discoverNearby(true) end, 0.32)


local function fastTravelMessageCallback(data)
    local followers = tableLib.copy(fastTravel.followers)
    fastTravel.followers = {}

    if next(fightingActors) ~= nil and debug.isAIEnabled() then
        ui.showMessage(l10n("fastTravelWhileInCombat"))
        return
    end

    local fTravelMult = core.getGMST("fTravelMult") or 4000
    local fTravelTimeMult = core.getGMST("fTravelTimeMult") or 16000

    local fastTravelCurrency = configLib.data.fastTravel.currency
    data.currency = fastTravelCurrency

    local isCurrencyMagicka = fastTravelCurrency == "FTCurrencyMagicka"
    local isCurrencyGold = fastTravelCurrency == "FTCurrencyGold"
    local isCurrencyHealth = fastTravelCurrency == "FTCurrencyHealth"
    local isCurrencyFree = fastTravelCurrency == "FTCurrencyFree"

    local cost = isCurrencyFree and 0 or configLib.data.fastTravel.baseMagickaCost
    local additionalCost = isCurrencyFree and 0 or configLib.data.fastTravel.additionalCost

    local capacity = math.max(1, types.Actor.getCapacity(self))
    local encumbrance = math.max(0, types.Actor.getEncumbrance(self))

    if isCurrencyMagicka then
        cost = cost + data.worldDistance / 16384 * additionalCost
        cost = cost + 2 * math.min(10, data.depthToPoint) * additionalCost
        cost = math.floor(math.max(0, cost * (2 - types.NPC.stats.skills.mysticism(self).base / 100)))
    elseif isCurrencyGold then
        cost = cost + (data.worldDistance + data.depthToPoint * 3072) / fTravelMult * additionalCost
    elseif isCurrencyHealth then
        cost = cost + (data.worldDistance + data.depthToPoint * 3072) / 8192 * additionalCost
    end

    cost = cost + math.max(0, (encumbrance - capacity) / 10) * additionalCost
    cost = cost * (1 + 0.5 * #followers)
    if data.isInSameInteriorBlock then
        cost = cost * 0.66
    end

    local plSpeedMul = math.sqrt(100 / util.clamp(types.Actor.stats.attributes.speed(self).modified + 25, 25, 400))
    local encumbranceMul = util.clamp(encumbrance / capacity + 0.25, 0.5, 3)
    local travelTime = configLib.data.fastTravel.passTime and core.API_REVISION >= 111 and
        util.round((data.worldDistance + data.depthToPoint * 3072) / fTravelTimeMult * math.max(0.5, plSpeedMul * encumbranceMul)) or 0

    local eventData = {
        currency = fastTravelCurrency,
        cost = cost,
        cell = pDoor.destCell(data.targetDoor),
        position = pDoor.destPosition(data.targetDoor),
        rotation = pDoor.destRotation(data.targetDoor),
        message = data.message,
        followers = followers,
        travelTime = travelTime,
    }

    if not eventData.cell then return end

    if eventSys.triggerEvent(eventSys.EVENT.onFastTravelResolve, eventData) then
        return
    end

    local message = eventData.message or ""
    if cost > 0 or travelTime > 0 then
        local currencyMessageId = isCurrencyMagicka and "fastTravelMagickaCost" or
            isCurrencyGold and "fastTravelGoldCost" or
            isCurrencyHealth and "fastTravelHealthCost" or "fastTravelMagickaCost"

        message = message.."\n"..l10n("fastTravelCostMessage"):format(
            cost > 0 and l10n(currencyMessageId, {count = cost}):format(cost) or "",
            cost > 0 and travelTime > 0 and l10n("fastTravelAnd") or "",
            travelTime > 0 and l10n("fastTravelTimeCost", {count = travelTime}):format(travelTime) or ""
        )
    end

    menuHandler.registerMenu(commonData.messageBoxMenuId, messageBox.newSimple{
        message = message,
        relativeSize = util.vector2(0.25, 0.2),
        yesCallback = function ()
            if isCurrencyMagicka then
                local currentMagicka = types.Actor.stats.dynamic.magicka(self).current
                if currentMagicka < eventData.cost then
                    ui.showMessage(l10n("NotEnoughMagicka"))
                    menuHandler.destroyMenu(commonData.mapMenuId)
                    return
                end

                types.Actor.stats.dynamic.magicka(self).current = math.max(0, currentMagicka - eventData.cost)

            elseif isCurrencyGold then
                local inventory = types.Actor.inventory(self)
                local gold = inventory:find("gold_001")
                if not gold or gold.count < eventData.cost then
                    ui.showMessage(l10n("NotEnoughGold"))
                    menuHandler.destroyMenu(commonData.mapMenuId)
                    return
                end

                core.sendGlobalEvent("AdvWMap:removeItem", {item = gold, count = eventData.cost})

            elseif isCurrencyHealth then
                local currentHealth = types.Actor.stats.dynamic.health(self).current
                if currentHealth < eventData.cost then
                    ui.showMessage(l10n("NotEnoughHealth"))
                    menuHandler.destroyMenu(commonData.mapMenuId)
                    return
                end

                types.Actor.stats.dynamic.health(self).current = math.max(0, currentHealth - eventData.cost)
            end

            if configLib.data.fastTravel.withFollowers then
                data.followers = followers
            end

            eventData.followers = data.followers
            data.travelTime = eventData.travelTime
            eventSys.triggerEvent(eventSys.EVENT.onFastTravelResolved, eventData)

            discoveredLocs.blockDiscovery = true
            core.sendGlobalEvent("AdvWMap:fastTravelTeleport", data)
            async:newUnsavableSimulationTimer(0.5, function ()
                discoveredLocs.blockDiscovery = false

                if not types.Player.isTeleportingEnabled(self) then

                    --- this doesn't work as expected
                    -- types.Player.setTeleportingEnabled(self, true)
                    -- core.sendGlobalEvent("AdvWMap:fastTravelTeleport", {
                    --     position = pPos,
                    --     cellId = pCellId,
                    --     followers = data.followers
                    -- })

                    ui.showMessage(core.getGMST("sTeleportDisabled") or "")
                end
            end)

            if isCurrencyMagicka and I.SkillProgression then
                I.SkillProgression.skillUsed("mysticism", {
                    useType = I.SkillProgression.SKILL_USE_TYPES.Spellcast_Success,
                    scale = util.clamp(eventData.cost / 50, 1, 3)
                })
            end

            localStorage.data[commonData.fastTravelTimestampFieldId] = core.getGameTime()
            fastTravel.lastTimestamp = core.getRealTime()

            if not localStorage.data[commonData.pinnedStateFieldId] then
                menuHandler.destroyMenu(commonData.mapMenuId)
            else
                menuMode.deactivate()
            end
        end,
    })
end


local function hotkeyCloseMenu()
    local messageMenu = menuHandler.getMenu(commonData.messageBoxMenuId)
    if messageMenu then
        menuHandler.destroyMenu(commonData.messageBoxMenuId)
    end

    local pinned = localStorage.data[commonData.pinnedStateFieldId]
    if pinned and menuMode.isMenuInteractive() then
        local menu = menuHandler.getMenu(commonData.mapMenuId)
        if menu then
            menuMode.deactivate()
            return
        end
    end

    if not pinned then
        closeMenu()
    end
end


local function deactivateHotkeys()
    hotkeyLayers.register{
        id = commonData.hotkeyLayerDeactivateDefault,
        priority = 9999999,
    }
    hotkeyLayers.register{
        id = commonData.hotkeyLayerDeactivateMain,
        group = "main",
        priority = 9999999,
    }
end

local function restoreHotkeys()
    hotkeyLayers.unregister(commonData.hotkeyLayerDeactivateDefault)
    hotkeyLayers.unregister(commonData.hotkeyLayerDeactivateMain)
end

local function areHotkeysDeactivated()
    return not hotkeyLayers.isActive(commonData.hotkeyLayerDeactivateDefault) and
        not hotkeyLayers.isActive(commonData.hotkeyLayerDeactivateMain) or false
end



local lastPlayerCellId
local menuStateUpdateTimer

return {
    interfaceName = "AdvancedWorldMap",
    ---@type AdvancedWorldMap.Interface
    interface = {
        version = 24,
        events = require("scripts.advanced_world_map.eventSys"),
        getConfig = function ()
            return configLib.data
        end,
        setConfigValue = function (valuePath, value)
            configLib.setValue(valuePath, value)
        end,
        openMapMenu = openMenu,
        closeMapMenu = function (force)
            local menu = menuHandler.getMenu(commonData.mapMenuId)
            if menu then
                if force then
                    menuHandler.destroyMenu(commonData.mapMenuId)
                elseif localStorage.data[commonData.pinnedStateFieldId] and not menu.isInActiveMode then
                    if menu.isInActiveMode then
                        menuMode.deactivate()
                        menu:updateInteractiveElements()
                        menu:update()
                    end
                elseif not menu:close() then
                    menuHandler.unregister(commonData.mapMenuId)
                end
            end
            local fiMenu = menuHandler.getMenu(commonData.firstInitMenuId)
            if fiMenu then
                fiMenu.params.yesCallback = nil
                menuHandler.destroyMenu(commonData.firstInitMenuId)
            end
        end,
        toggleMapMenu = toggleMenu,
        getMapMenu = getMenu,
        setHotkeysActive = function (active)
            if active then
                restoreHotkeys()
            else
                deactivateHotkeys()
            end
        end,
        isHotkeysActive = function ()
            return areHotkeysDeactivated()
        end,
        hasActiveMenus = function ()
            return menuHandler.hasActiveMenus()
        end,
        isDiscovered = function (cellId)
            return discoveredLocs.isDiscovered(cellId)
        end,
        isPositionDiscovered = function (cellId, pos)
            return discoveredLocs.isPositionDiscovered(cellId, pos)
        end,
        isVisited = function (cellId)
            return discoveredLocs.isVisited(cellId)
        end,
        isMapDataInitialized = function ()
            return mapDataHandler.isInitialized()
        end,
        getCellNameById = function (cellId)
            return mapDataHandler.cellNameById[cellId]
        end,
        getExteriorCellName = function (pos)
            return mapDataHandler.cellNameById[cellLib.getCellIdByPos(pos)]
        end,
        getEntranceMarkerData = function (cellId)
            return mapDataHandler.entrances[cellId]
        end,
        getTransportData = function ()
            return mapDataHandler.transport ---@diagnostic disable-line: return-type-mismatch
        end,
        getDoorHash = function (doorRef, destCellId)
            return commonData.doorHash(doorRef, destCellId)
        end,
        setWorldMapInfo = function (mapInfo, dirPath, mapImagePath)
            return mapTextureHandler.setWorldMapInfo(mapInfo, dirPath, mapImagePath)
        end,
        getWorldMapInfo = function ()
            ---@diagnostic disable-next-line: return-type-mismatch
            return mapTextureHandler.mapInfo, mapTextureHandler.mapDir, mapTextureHandler.mapImagePath
        end,
        _clearCache = function ()
            mapMenu.clearMapWidgetCache()
        end,
        uiElements = {
            scrollBox = require("scripts.advanced_world_map.ui.scrollBox"),
            borders = require("scripts.advanced_world_map.ui.borders"),
            button = require("scripts.advanced_world_map.ui.button"),
            interval = require("scripts.advanced_world_map.ui.interval"),
            checkbox = require("scripts.advanced_world_map.ui.checkBox"),
            tooltip = require("scripts.advanced_world_map.ui.tooltip"),
        },
        realTimer = realTimer.newTimer,
    },
    engineHandlers = {
        onSave = onSave,
        onLoad = onLoad,
        onInit = onInit,
        onTeleported = function ()
            async:newUnsavableSimulationTimer(0.01, function ()
                if not self.cell then return end
                addNearbyDoors()
                discoverNearby()
                if not self.cell.isExterior then
                    playerPos.updateExteriorPos()
                end
            end)
        end,
        onFrame = function(dt)
            if pendingDoors then finishDoorQuery(false) end
            realTimer.updateTimers()
        end,
        onMouseWheel = onMouseWheel,
        onKeyPress = function (key)
            keysModule.isGamepad = false
            if key.code == input.KEY.Escape and menuHandler.hasActiveMenus() then
                realTimer.newTimer(0, function ()
                    hotkeyCloseMenu()
                end)
            end
        end,
        onControllerButtonPress = function (buttonId)
            keysModule.isGamepad = true
            if buttonId == input.CONTROLLER_BUTTON.B and menuHandler.hasActiveMenus() then
                realTimer.newTimer(0, function ()
                    hotkeyCloseMenu()
                end)
            end
        end,
        onMouseButtonPress = function ()
            keysModule.isGamepad = false
        end,
        onMouseButtonRelease = function (buttonId)
            menuHandler.onMouseReleaseCallback(buttonId)
        end,
    },
    eventHandlers = {
        -- when changing location, a loading menu is displayed, which triggers this event
        UiModeChanged = function(e)
            if menuStateUpdateTimer then
                menuStateUpdateTimer()
            end
            menuStateUpdateTimer = realTimer.newTimer(0.1, function ()
                if not menuMode.isMenuInteractive() then
                    menuMode.setActivatedFlag(false)
                end
                local menu = menuHandler.getMenu(commonData.mapMenuId)
                if menu and menu:updateInteractiveElements() then
                    menu:update()
                end
                menuStateUpdateTimer = nil
            end)

            if e.oldMode == "Loading" or e.oldMode == nil and e.newMode == nil and lastPlayerCellId ~= self.cell.id then
                lastPlayerCellId = self.cell.id

                core.sendGlobalEvent("AdvWMap:requestTimeUpdate", self.object)
                addNearbyDoors()
                discoverNearby()

                local newVisited = discoveredLocs.addVisitedCell(self.cell)
                if newVisited then
                    markers.updateDiscovered(newVisited)
                end
                local newDiscovered = discoveredLocs.addDiscoveredCell(self.cell, true)
                if newDiscovered then
                    markers.updateDiscovered(newDiscovered)
                end

                local cellId = not self.cell.isExterior and self.cell.id or nil

                local menu = menuHandler.getMenu(commonData.mapMenuId)
                if menu and menu.centerOnPlayer then
                    if menu.mapWidget.cellId == cellId then
                        menu.mapWidget:updateOnZoomMarkers()
                    else
                        menu:updateMapWidgetCell(cellId)
                    end
                    cellNameWidget.updateLabel(menu)
                    menu:update()
                end

                core.sendGlobalEvent("AdvWMap:cellChanged", self.object)
            elseif e.newMode == "Loading" then
                -- update timestamp of the visited cell before the player leaves it
                discoveredLocs.updateVisited(self.cell)
            elseif mapDataHandler.isInitialized() and e.oldMode ~= nil and e.newMode == nil and not menuHandler.getMenu(commonData.mapMenuId) and
                    (localStorage.data[commonData.inMinimapModeKeyId] == true or localStorage.data[commonData.pinnedStateFieldId]) then
                if not isShouldShowFirstInit() then
                    openMenu(false, true, config.data.main.overrideDefault)
                end
            end

            if e.newMode then
                lastUiModeTimestamp = core.getRealTime()
                lastUiModeId = e.newMode
            end
        end,

        DialogueResponse = function(e)
            if e.type ~= "topic" and e.type ~= "greeting" then return end

            local newDiscovered = discoveredLocs.addFromDialogueScript(e.recordId, e.infoId)
            if newDiscovered then
                markers.updateDiscovered(newDiscovered)
            end
        end,

        OMWMusicCombatTargetsChanged = onCombatTargetsChanged,

        -- for compatibility with mods that change player chargen state
        ["AdvWMap:toggleMenuCheck"] = function (data)
            if types.Player.isCharGenFinished(self) or data.isCharGenFinished then
                toggleMenu()
            else
                ui.showMessage(l10n("charGenNotFinished"))
            end
        end,

        ["AdvWMap:initMapData"] = function (data)
            if mapDataHandler.playerInit(self, data and data.cellCount, data and data.options) then
                mapTextureHandler.init()
            end
        end,

        ["AdvWMap:updateMapData"] = function (data)
            mapDataHandler.updateData(self, data)
            mapTextureHandler.init()
        end,

        ["AdvWMap:processMapDataOptions"] = function (options)
            if not options then return end

            if options.toggleMenu then
                toggleMenu()
            end

            if options.openMenu then
                openMenu(options.openInMenuMode, true)
            end
        end,

        ["AdvWMap:showMessage"] = function (str)
            ui.showMessage(str)
        end,

        ["AdvWMap:playSound"] = function (data)
            ambient.playSound(data.soundId, {})
        end,

        ["AdvWMap:fastTravelMessage"] = function (data)
            realTimer.newTimer(0.2, function ()
                fastTravelMessageCallback(data)
            end)
        end,

        ["AdvWMap:fastTravelFollowerData"] = function (data)
            if not data.actor then return end

            table.insert(fastTravel.followers, data.actor)
        end,

        ["AdvWMap:cancelAnimation"] = function (data)
            if not data or not data.groupName then return end
            animation.cancel(self, data.groupName)
        end,

        ["AdvWMap:registerDisabledDoor"] = function(ref)
            disabledDoors.register(ref)
            markers.updateDoorMarkerVisibility(ref)
        end,

        ["AdvWMap:unregisterDisabledDoor"] = function(ref)
            disabledDoors.unregister(ref)
            markers.updateDoorMarkerVisibility(ref)
        end,

        ["AdvWMap:registerDisabledActor"] = function(recordId)
            local wasDisabled = disabledActors.contains(recordId)
            disabledActors.register(recordId)
            if not wasDisabled and mapDataHandler.transport and mapDataHandler.transport.actors[recordId] then
                local mapWidget = mapMenu.cachedMapWidgetMetatable[commonData.exteriorMapId]
                if mapWidget and mapWidget.userData.updateTravelMarkers then
                    mapWidget.userData.updateTravelMarkers(mapDataHandler.transport.actors[recordId].tp)
                end
            end
        end,

        ["AdvWMap:unregisterDisabledActor"] = function(recordId)
            local wasDisabled = disabledActors.contains(recordId)
            disabledActors.unregister(recordId)
            if wasDisabled and mapDataHandler.transport and mapDataHandler.transport.actors[recordId] then
                local mapWidget = mapMenu.cachedMapWidgetMetatable[commonData.exteriorMapId]
                if mapWidget and mapWidget.userData.updateTravelMarkers then
                    mapWidget.userData.updateTravelMarkers(mapDataHandler.transport.actors[recordId].tp)
                end
            end
        end,

        ["AdvWMap:getMapStatics"] = function (data)
            local menu = menuHandler.getMenu(commonData.mapMenuId)
            if menu then
                menu.mapWidget.cellStatics = data.res
                menu.mapWidget:updateOnZoomMarkers(true)
                menu:update()
            end
        end,

        ["AdvWMap:requestTimeUpdate"] = function (data)
            dateLib.setGlobalTime(data.day, data.month, data.year)
        end,

        ["AdvWMap:tmCall"] = function (id)
            realTimer.executeTimer(id)
        end,

        ["AdvWMap:updateSearchResults"] = function (dt)
            local menu = menuHandler.getMenu(commonData.mapMenuId)
            if not menu or not menu.userData.advWMapSetSearchResults then return end
            if dt.mode ~= localStorage.data[commonData.searchModeFieldId] then return end

            local results = tableLib.values(dt.results)
            for _, dt in pairs(results) do
                dt.text = string.format("%s (%s)", dt.name, dt.type)
                dt.priority = 99999 - dt.i
            end

            menu.userData.advWMapSetSearchResults(results)
        end,

        ["AdvWMap:northMarkerData"] = function (data)
            northMarker.set(data.cellId, data.yaw)
        end
    },
}