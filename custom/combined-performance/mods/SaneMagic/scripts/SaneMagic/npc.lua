local self = require('openmw.self')
local types = require('openmw.types')
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local AI = require('openmw.interfaces').AI
local I = require('openmw.interfaces')
local storage = require('openmw.storage')
local settings = storage.globalSection('SaneMagicSettingsGlobal')
local msg = core.l10n('SaneMagic', 'en')
local lctn = require('Scripts.SaneMagic.location')

local skills = types.NPC.stats.skills
local EFF = core.magic.EFFECT_TYPE

local underEffect = {}
local hasEffect = {}
local charmCaster
local combatReported = false
local lastCombat
local lastCombatPlayer
-- This actor-bound native view reads the current spell store on each iteration.
local activeSpells
local effectSourceQuery

local function resetCombatReport()
    activeSpells = nil
    effectSourceQuery = nil
    combatReported = false
    lastCombatPlayer = nil
end
local charmCount = 0
local charmFailCount = 0
local sneakLimit = nil

local function string_ends_with(str, ending)
    local len = string.len(str)
    local end_len = string.len(ending)
    if end_len > len then
        return false
    end
    return string.sub(str, -end_len) == ending
end

local fightingPlayer
local function inspectPackage(param)
    if param.target and param.target.type == types.Player and param.type == "Combat" then
        fightingPlayer = true
    end
end

local function isFightingPlayer()
    fightingPlayer = nil
    I.AI.forEachPackage(inspectPackage)
    return fightingPlayer
end

local function checkPlayerSneak(pl)
    if not pl or not pl:isValid() then return end
    local sneak = skills.sneak(pl).modified
    local class = types.NPC.record(self).class

    local isTrader = false
    if class == "trader service" or class == "bookseller" or class == "pawnbroker" or string_ends_with(class, "service") then
        isTrader = true
    end

    if sneakLimit == nil then
        sneakLimit = settings:get("smCharmSneakLimit") or 25
    end

    if sneak < sneakLimit then
        charmFailCount = charmFailCount + 1
        if charmFailCount == 1 then
            types.NPC.setBaseDisposition(self, pl, 0)
            if isTrader then
                types.NPC.stats.skills.mercantile(self).base = 100
            end
        else
            if isTrader then
                core.sendGlobalEvent("punishCharm", {
                    victim = self
                })
            end

            AI.startPackage({
                type = 'Combat',
                target = pl
            })
        end
    end
    if sneak >= sneakLimit then
        charmCount = charmCount + 1
        sneakLimit = math.min(sneakLimit + 10, 100) -- лимит 100
        if sneakLimit == 100 then
            types.NPC.setBaseDisposition(self, pl, 0)
        else
            pl:sendEvent('smShowMessage', {
                message = msg("smNextCharm", {
                    sneak = sneakLimit
                })
            })
        end
    end
end

local randomFirstCharm = {"smFirstCharm", "smFirstCharm2"}
local function observeEffect(effectId, caster)
    if effectId == EFF.Charm then
        charmCaster = caster
    end
    if not underEffect[effectId] then
        underEffect[effectId] = true
        if effectId == EFF.FrenzyHumanoid
           and settings:get('smSuspiciousEffect')
           and lctn.isFrenzyIllegal({
               cellId = self.cell.id,
               cellName = self.cell.name,
               cellRegion = self.cell.region,
           })
           and not isFightingPlayer() then
            caster:sendEvent('smFrenzyOnMe', {
                victim   = self,
                effectId = effectId,
            })
        elseif (effectId == EFF.Charm) and settings:get("smCharm") then
            local sneak = skills.sneak(caster).modified
            if sneakLimit == nil then
                sneakLimit = settings:get("smCharmSneakLimit") or 25
            end

            if sneakLimit == 100 then
                AI.startPackage({
                    type = 'Combat',
                    target = caster
                })
            elseif sneak >= sneakLimit then
                local msgFirstCharm = randomFirstCharm[math.random(1, #randomFirstCharm)]
                caster:sendEvent('smShowMessage', {
                    message = msg(msgFirstCharm)
                })
            else
                caster:sendEvent('smShowMessage', {
                    message = msg("smFirstCharmNotEnough")
                })
            end
        end
    end
    hasEffect[effectId] = true
end

local function checkEffectsOnNPC(spells)
    for effectId in pairs(hasEffect) do
        hasEffect[effectId] = nil
    end

    if effectSourceQuery then
        -- Native selection preserves spell/effect order and duplicates. The
        -- immutable ID/caster arrays are reused only when their live values are
        -- unchanged; gameplay checks below still run at their original cadence.
        local effectIds, casters = effectSourceQuery:read()
        for index, effectId in ipairs(effectIds) do
            observeEffect(effectId, casters[index])
        end
    else
        for spellid, param in pairs(spells) do
            local caster = param.temporary and param.caster
            if caster and caster.type == types.Player then
                for _, effect in ipairs(param.effects) do
                    observeEffect(effect.id, caster)
                end
            end
        end
    end

    for effectId, wasUnderEffect in pairs(underEffect) do
        if not hasEffect[effectId] and wasUnderEffect then
            underEffect[effectId] = false
            if (effectId == EFF.Charm) and settings:get("smCharm") then
                checkPlayerSneak(charmCaster)
                charmCaster = nil
            end
        end
    end
end

local function onUpdate(dt)
   if not types.NPC.objectIsInstance(self) then
        return
    end

    local spells = activeSpells
    if not spells then
        local ok
        ok, spells = pcall(types.Actor.activeSpells, self)
        if not ok or not spells then
            return
        end
        activeSpells = spells
        local supported, createQuery = pcall(function() return spells.createEffectSourceQuery end)
        if supported and type(createQuery) == "function" then
            effectSourceQuery = createQuery(spells, { temporary = true, casterType = types.Player })
        end
    end

    checkEffectsOnNPC(spells)
    local isFighting = isFightingPlayer()
    local pl = nearby.players[1]
    if pl and (not combatReported or lastCombat ~= isFighting
            or lastCombatPlayer ~= pl) then
        pl:sendEvent('smInCombat', {
            npc = self.id,
            inCombat = isFighting
        })
        lastCombat = isFighting
        lastCombatPlayer = pl
        combatReported = true
    end
end

local function onLoad(data)
    resetCombatReport()
    charmCaster = data and data.charmCaster or nil
    charmCount = data and data.charmCount or 0
    charmFailCount = data and data.charmFailCount or 0
    sneakLimit = data and data.sneakLimit or settings:get("smCharmSneakLimit") or 25
    underEffect = data and data.underEffect or {}
end

local function onSave()
    return {
        charmCount = charmCount,
        charmFailCount = charmFailCount,
        sneakLimit = sneakLimit,
        underEffect = underEffect,
        charmCaster = charmCaster
    }
end

return {
    engineHandlers = {
        onUpdate = onUpdate,
        onActive = resetCombatReport,
        onInactive = function()
            -- Retain the handle: nearby cannot be queried while inactive.
            if combatReported and lastCombat and lastCombatPlayer
                    and lastCombatPlayer:isValid() then
                lastCombatPlayer:sendEvent('smInCombat', { npc = self.id })
            end
            resetCombatReport()
        end,
        onLoad = onLoad,
        onSave = onSave
    }
}
