local I = require('openmw.interfaces')
local types = require('openmw.types')
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local self = require('openmw.self')
local combat = require('openmw.interfaces').Combat
local util = require('openmw.util')
local storage = require('openmw.storage')
local conj = require('Scripts.SaneMagic.daeSumm_s')
local conjData = storage.globalSection('SaneMagicDaeSumm')

local active = {}
-- ActorStore is a live view; pairs resets its cursor for every scan.
-- Keep neither an iterator nor effect values between updates.
local activeEffects
local effectMagnitudeQuery
local magnitudeKeys = {}
local nativeMagnitudeQuery
local nativeHasEffects

local function resetEffectView()
    activeEffects = nil
    effectMagnitudeQuery = nil
    magnitudeKeys = {}
    nativeMagnitudeQuery = nil
    nativeHasEffects = nil
end

local SPAWN_Z_OFFSET     = 50
local GROUND_CHECK_Z     = 300
local WALL_CHECK_HEIGHT  = 60
local MAX_HEIGHT_DIFF    = 120
local SPAWN_CLEARANCE    = 10
local FALLBACK_HEIGHT    = 80
local DEFAULT_RADIUS     = 150

local GROUND_COLLISION = nearby.COLLISION_TYPE.World + nearby.COLLISION_TYPE.HeightMap
local WALL_COLLISION   = nearby.COLLISION_TYPE.World

local DIRECTIONS = {
    util.vector3( 1, 0, 0),
    util.vector3(-1, 0, 0),
    util.vector3( 0, 1, 0),
    util.vector3( 0,-1, 0),
}

local function findSpawnPos(actor, distance)
    local radius   = distance or DEFAULT_RADIUS
    local actorPos = actor.position
    local wallStart = actorPos + util.vector3(0, 0, WALL_CHECK_HEIGHT)

    for i, dir in ipairs(DIRECTIONS) do
        local candidate = actorPos + dir * radius
        local wallEnd   = candidate + util.vector3(0, 0, WALL_CHECK_HEIGHT)
        local wallCheck = nearby.castRay(wallStart, wallEnd, {
            collisionType = WALL_COLLISION,
            ignore        = {actor}
        })

        if wallCheck.hit then
        else
            local groundStart = candidate + util.vector3(0, 0, SPAWN_Z_OFFSET)
            local groundEnd   = candidate - util.vector3(0, 0, GROUND_CHECK_Z)

            local groundCheck = nearby.castRay(groundStart, groundEnd, {
                collisionType = GROUND_COLLISION
            })

            if groundCheck.hit and groundCheck.hitPos then
                local heightDiff = math.abs(groundCheck.hitPos.z - actorPos.z)

                if heightDiff < MAX_HEIGHT_DIFF then
                    local spawnPos = groundCheck.hitPos + util.vector3(0, 0, SPAWN_CLEARANCE)
                    return spawnPos
                else
                end
            else
            end
        end
    end

    local fallback = actorPos + util.vector3(0, 0, FALLBACK_HEIGHT)

    local fbGround = nearby.castRay(
        fallback + util.vector3(0, 0, SPAWN_Z_OFFSET),
        fallback - util.vector3(0, 0, GROUND_CHECK_Z),
        { collisionType = GROUND_COLLISION })

    if fbGround.hit and fbGround.hitPos then
    end

    return fallback
end

local function boundMagnitudes()
    if not nativeMagnitudeQuery then return nil end
    -- The source list is normally static, but honour in-place changes and its
    -- ordering rather than assuming that a retained Lua table is immutable.
    local count = 0
    local changed = effectMagnitudeQuery == nil
    for index, weapon in ipairs(conj.summonedWeaponsEffects) do
        count = index
        if magnitudeKeys[index] ~= weapon then changed = true end
    end
    if count ~= #magnitudeKeys then changed = true end
    if changed then
        magnitudeKeys = {}
        for index, weapon in ipairs(conj.summonedWeaponsEffects) do
            magnitudeKeys[index] = weapon
        end
        effectMagnitudeQuery = activeEffects:createMagnitudeQuery(magnitudeKeys)
    end
    return effectMagnitudeQuery:read()
end

local function onUpdate(dt)

    if core.isWorldPaused() then
        return
    end

    local mode = conjData:get('smDaeSumm')

    if mode == "Disabled" or mode == "DamageShare" then
        return
    end

    if not activeEffects then
        activeEffects = types.Actor.activeEffects(self)
        local ok, method = pcall(function() return activeEffects.createMagnitudeQuery end)
        nativeMagnitudeQuery = ok and type(method) == "function"
        ok, method = pcall(function() return activeEffects.hasEffects end)
        nativeHasEffects = ok and type(method) == "function"
    end

    -- An empty aggregate effect list cannot contain an active bound item.
    -- Keep the author's ordered lookups unchanged for nonempty lists.
    local anyEffects
    if nativeHasEffects then
        anyEffects = activeEffects:hasEffects()
    else
        local iterator, state, key = pairs(activeEffects)
        anyEffects = iterator(state, key) ~= nil
    end
    if not anyEffects then
        for _, weapon in ipairs(conj.summonedWeaponsEffects) do
            active[weapon] = nil
        end
        return
    end

    local magnitudes = boundMagnitudes()
    for index, weapon in ipairs(conj.summonedWeaponsEffects) do
        local magnitude
        if magnitudes then
            magnitude = magnitudes[index]
        else
            local effect = activeEffects:getEffect(weapon)
            magnitude = effect and effect.magnitude
        end
        if magnitude and magnitude > 0 then
            if not active[weapon] then
                active[weapon] = true
                local ownerPos = findSpawnPos(self, 150)
                core.sendGlobalEvent("smNewSummonDaeSumm", {
    summon = weapon,
    owner = self,
    cell = (self.cell.name ~= "" and self.cell.name) or nil,
    region = self.cell.isExterior and self.cell.region or nil,
    pos = self.position,
    ownerPos = ownerPos,
    ownerCell = (self.cell.name ~= "" and self.cell.name) or nil
})
            end
        else
            active[weapon] = nil
        end
    end
end

local STAT_ORDER = {
    Magicka = {'magicka', 'fatigue', 'health'},
    Fatigue = {'fatigue', 'magicka', 'health'},
    Health = {'health'}
}

local function applyDaeSummDamage(actor, healthLostPercent, kDamage, mode)
    if healthLostPercent <= 0 or kDamage <= 0 then
        return
    end

    local damagePercent = healthLostPercent * kDamage

    local order = STAT_ORDER[mode] or STAT_ORDER.Health
    local remainingPercent = damagePercent
    local dynamicStats = types.Actor.stats.dynamic

    for _, statName in ipairs(order) do
        if remainingPercent <= 0 then
            break
        end

        local stat = dynamicStats[statName](actor)
        local maxValue = stat.base
        local currentValue = stat.current

        if maxValue > 0 and currentValue > 0 then
            local maxPossibleDamage = maxValue * remainingPercent
            local actualDamage = math.min(currentValue, maxPossibleDamage)

            stat.current = currentValue - actualDamage

            local actualPercentDrained = actualDamage / maxValue
            remainingPercent = remainingPercent - actualPercentDrained
        end
    end
end

return {
    engineHandlers = {
        onUpdate = onUpdate,
        onInactive = resetEffectView,
        onSave = function()
            return {
                activeSummons = active
            }
        end,
        onLoad = function(data)
            resetEffectView()
            active = (data and data.activeSummons) or {}
        end
    },

    eventHandlers = {
        smDaeSummHurt = function(data)

            if not types.Player.objectIsInstance(self) then
                return
            end

            local mode = data.damageType or conjData:get('smDaeSummDamageType') or 'Health'

            local healthLostPercent = data.healthLostPercent or 0
            local kDamage = data.kDamage or 0

            if healthLostPercent <= 0 or kDamage <= 0 then
                return
            end

            applyDaeSummDamage(self, healthLostPercent, kDamage, mode)

            local health = types.Actor.stats.dynamic.health(self)
            if mode == 'Health' or health.current < health.base then
                combat.spawnBloodEffect(self.position)
            end
        end
    }
}
