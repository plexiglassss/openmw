#!/usr/bin/env python3
"""Compare the supplied SaneMagic adapters with the captured installed scripts.

This runs actual Lua 5.1 and LuaJIT 2.1 with controlled game services. Native
query correctness is covered separately by openmw-tests; these cases verify
that API adoption does not change the mods' observable behaviour or cadence.
The installed originals are preserved as byte-verified gzip fixtures. An explicitly
supplied baseline directory can also contain raw Lua files with the same captured hashes.
No game installation or save is written.
"""
import argparse
import gzip
import hashlib
import importlib
import importlib.metadata
import json
from pathlib import Path


BASELINE_HASHES = {
    "npc.lua": "f9a3e0e338f8b34319ece5106ed189f79a575fdc5c3874dbb2c5e7502a57a7be",
    "daesumm_pn.lua": "a5d741551d4fac3b62f46fe6ed2b3480e62aa7c47b079e9e14ec6dd611c7e12c",
}

SETUP = r'''
function setup(native)
    math.randomseed(1937)
    local realPairs = pairs
    pairs = function(value)
        local mt = getmetatable(value)
        if mt and mt.__pairs then return mt.__pairs(value) end
        return realPairs(value)
    end
    S = {
        native = native, events = {}, spells = {}, effects = {}, packages = {},
        paused = false, illegal = true, sneak = 40, valid = true,
        settings = {smCharm = true, smCharmSneakLimit = 25, smSuspiciousEffect = true},
        summon = {smDaeSumm = 'Summon'}, counters = {}, weapons = {'bound_a','bound_b'},
        mercantile = {base = 20}, snapshots = {},
    }
    local function count(key) S.counters[key] = (S.counters[key] or 0) + 1 end
    local vector
    local vmeta = {
        __add = function(a,b) return vector(a.x+b.x,a.y+b.y,a.z+b.z) end,
        __sub = function(a,b) return vector(a.x-b.x,a.y-b.y,a.z-b.z) end,
        __mul = function(a,b) return vector(a.x*b,a.y*b,a.z*b) end,
    }
    vector = function(x,y,z) return setmetatable({x=x,y=y,z=z},vmeta) end
    local types = {Player = {name='Player'}, NPC = {name='NPC'}, Actor = {}}
    local function actor(id, tp)
        return {
            __actor=true,id=id,type=tp,recordId=id,position=vector(1,2,3),
            cell={id='cell',name='Town',region='Region',isExterior=true},
            isValid=function() return S.valid end,
            sendEvent=function(self,name,data)
                table.insert(S.events,{recipient=self.id,name=name,data=data})
            end,
        }
    end
    S.self = actor('npc',types.NPC)
    S.player = actor('player',types.Player)
    S.otherPlayer = actor('other_player',types.Player)
    S.other = actor('other',types.NPC)
    local function aggregate(id)
        local value = S.effects[id]
        if type(value)=='table' then return (value.base or 0)+(value.modifier or 0) end
        return value or 0
    end
    local function hasEffects()
        for _,v in realPairs(S.effects) do
            if type(v)=='table' then
                if (v.base or 0)~=0 or (v.modifier or 0)~=0 then return true end
            elseif v~=0 then return true end
        end
        return false
    end
    local spells = setmetatable({}, {__pairs=function()
        count('legacy_spell_reads')
        return ipairs(S.spells)
    end})
    local effects = setmetatable({}, {__pairs=function()
        count('legacy_effect_iterations')
        local values={}
        for id,value in realPairs(S.effects) do
            local included = type(value)=='table' and
                ((value.base or 0)~=0 or (value.modifier or 0)~=0) or
                (type(value)=='number' and value~=0)
            if included then values[#values+1]={id=id,magnitude=aggregate(id)} end
        end
        return ipairs(values)
    end})
    effects.getEffect = function(_,id)
        count('legacy_effect_gets')
        return {magnitude=aggregate(id)}
    end
    if native then
        spells.createEffectSourceQuery = function(_,options)
            count('source_query_creates')
            local previousIds, previousCasters
            return {read=function()
                count('source_query_reads')
                local ids,casters={},{}
                for _,spell in ipairs(S.spells) do
                    if spell.temporary==options.temporary and spell.caster and
                            spell.caster.type==options.casterType then
                        for _,effect in ipairs(spell.effects) do
                            ids[#ids+1]=effect.id
                            casters[#casters+1]=spell.caster
                        end
                    end
                end
                local changed=not previousIds or #previousIds~=#ids
                if not changed then
                    for i,id in ipairs(ids) do
                        if previousIds[i]~=id or previousCasters[i]~=casters[i] then changed=true end
                    end
                end
                if changed then
                    count('source_snapshot_builds')
                    previousIds,previousCasters=ids,casters
                end
                return previousIds,previousCasters,changed
            end}
        end
        effects.hasEffects = function() count('native_presence_reads'); return hasEffects() end
        effects.createMagnitudeQuery = function(_,keys)
            count('magnitude_query_creates')
            local selected={}
            for i,key in ipairs(keys) do selected[i]=key end
            local previous
            return {read=function()
                count('magnitude_query_reads')
                local values={}
                local changed=previous==nil
                for i,key in ipairs(selected) do
                    values[i]=aggregate(key)
                    if previous and previous[i]~=values[i] then changed=true end
                end
                if changed then previous=values;count('magnitude_snapshot_builds') end
                return previous,changed
            end}
        end
    end
    types.Actor.activeSpells=function() count('spell_view_creates');return spells end
    types.Actor.activeEffects=function() count('effect_view_creates');return effects end
    types.NPC.objectIsInstance=function() return true end
    types.Player.objectIsInstance=function(object) return object.type==types.Player end
    types.NPC.record=function() return {class='trader service'} end
    types.NPC.setBaseDisposition=function(_,player,value)
        table.insert(S.events,{name='setDisposition',player=player.id,value=value})
    end
    types.NPC.stats={skills={
        sneak=function() return {modified=S.sneak} end,
        mercantile=function() return S.mercantile end,
    }}
    types.Actor.stats={dynamic={}}
    for _,name in ipairs({'health','magicka','fatigue'}) do
        local value={base=100,current=100}
        types.Actor.stats.dynamic[name]=function() return value end
    end
    local ai={
        forEachPackage=function(fn) for _,package in ipairs(S.packages) do fn(package) end end,
        startPackage=function(package)
            table.insert(S.events,{name='startPackage',data=package})
            S.packages={package}
        end,
    }
    local core={
        magic={EFFECT_TYPE={Charm='charm',FrenzyHumanoid='frenzyhumanoid'}},
        isWorldPaused=function() return S.paused end,
        sendGlobalEvent=function(name,data) table.insert(S.events,{name=name,data=data}) end,
        l10n=function() return function(name,args) return {name=name,args=args} end end,
    }
    local nearby={
        players={S.player}, COLLISION_TYPE={World=1,HeightMap=2},
        castRay=function(a,b,options)
            table.insert(S.events,{name='ray',a=a,b=b,options=options})
            return {hit=false}
        end,
    }
    local modules={
        ['openmw.self']=S.self,['openmw.types']=types,['openmw.core']=core,
        ['openmw.nearby']=nearby,['openmw.util']={vector3=vector},
        ['openmw.interfaces']={AI=ai,Combat={spawnBloodEffect=function() end}},
        ['openmw.storage']={globalSection=function(name)
            return {get=function(_,key)
                if name=='SaneMagicSettingsGlobal' then return S.settings[key] end
                return S.summon[key]
            end}
        end},
        ['Scripts.SaneMagic.location']={isFrenzyIllegal=function() return S.illegal end},
        ['Scripts.SaneMagic.daeSumm_s']={summonedWeaponsEffects=S.weapons},
    }
    require=function(name) assert(modules[name],name);return modules[name] end
    local function encode(value)
        if type(value)=='string' then return string.format('%q',value) end
        if type(value)~='table' then return tostring(value) end
        if value.__actor then return 'actor('..value.id..')' end
        local keys={}
        for key in realPairs(value) do keys[#keys+1]=key end
        table.sort(keys,function(a,b) return tostring(a)<tostring(b) end)
        local parts={}
        for _,key in ipairs(keys) do parts[#parts+1]=encode(key)..'='..encode(value[key]) end
        return '{'..table.concat(parts,',')..'}'
    end
    S.capture=function(label,handlers)
        S.snapshots[#S.snapshots+1]=encode({
            label=label,events=S.events,save=handlers.onSave and handlers.onSave(),
            mercantile=S.mercantile,
        })
    end
end
'''

NPC_SCENARIO = r'''
local handlers=module.engineHandlers
local function tick(label) handlers.onUpdate(0.016);S.capture(label,handlers) end
local function spell(caster,temporary,ids)
    local effects={};for _,id in ipairs(ids) do effects[#effects+1]={id=id} end
    return {caster=caster,temporary=temporary,effects=effects}
end
handlers.onActive()
tick('empty first update')
tick('empty unchanged update')
S.spells={spell(S.other,true,{'charm'}),spell(S.player,false,{'charm'})}
tick('irrelevant caster and permanent spell')
S.spells={spell(S.player,true,{'frenzyhumanoid','charm','other'}),
          spell(S.otherPlayer,true,{'charm'})}
tick('ordered duplicate effects and second player')
tick('unchanged effects still observed')
S.sneak=10
S.spells={spell(S.player,true,{'other'})}
tick('removal checks current player skill')
S.spells={spell(S.player,true,{'charm'})}
tick('first reapplication')
S.spells={}
tick('second failed removal escalates punishment')
S.spells={spell(S.player,true,{'frenzyhumanoid','charm'})}
tick('combat blocks frenzy report')
S.settings.smCharm=false
tick('settings change while query result unchanged')
S.settings.smCharm=true
S.spells={}
tick('settings reenabled before removal')
handlers.onInactive();S.capture('inactive combat report',handlers)
local save=handlers.onSave()
handlers.onLoad(save);handlers.onActive()
tick('saved state and active reload')
'''

SUMMON_SCENARIO = r'''
local handlers=module.engineHandlers
local function tick(label) handlers.onUpdate(0.016);S.capture(label,handlers) end
tick('empty initial store')
S.effects={bound_a=1};tick('first bound weapon')
tick('unchanged bound weapon')
S.effects.bound_b=2;tick('second bound weapon')
S.effects.bound_a=0;tick('first effect removed')
S.effects.bound_a=3;tick('first effect reapplied')
S.paused=true;S.effects={};tick('paused state does not clear active state')
S.paused=false;tick('unpaused empty store clears active state')
S.summon.smDaeSumm='Disabled';S.effects={bound_a=1};tick('disabled mode')
S.summon.smDaeSumm='DamageShare';tick('damage share mode')
S.summon.smDaeSumm='Summon';tick('summons reenabled')
S.effects={bound_a={base=5,modifier=-5},bound_b=-1};tick('zero total with nonzero components')
S.weapons[1]='bound_b';S.weapons[2]='bound_a'
S.effects={bound_a=1,bound_b=1};tick('in-place selection reorder')
S.weapons[1]='bound_a';S.weapons[2]='bound_a';S.weapons[3]='bound_b'
tick('duplicate selections do not duplicate summons')
S.weapons[1]=nil;S.weapons[2]=nil;S.weapons[3]=nil;tick('empty selection')
S.weapons[1]='bound_b';tick('selection regrows')
local save=handlers.onSave();handlers.onLoad(save);tick('saved active summons survive reload')
handlers.onInactive();tick('view recreated after inactivity')
'''


def run(runtime_name, source, native, scenario):
    LuaRuntime = importlib.import_module(runtime_name).LuaRuntime
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(SETUP)
    lua.globals().setup(native)
    loader = lua.eval("function(source) return assert(loadstring(source))() end")
    lua.globals().module = loader(source)
    lua.execute(scenario)
    snapshots = lua.globals().S.snapshots
    counters = lua.globals().S.counters
    return [snapshots[i] for i in range(1, len(snapshots) + 1)], dict(counters.items())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--baseline-dir", type=Path, default=Path(__file__).resolve().parent / "fixtures/SaneMagic")
    ap.add_argument("--mod-dir", type=Path, default=Path(__file__).resolve().parents[1] / "mods/SaneMagic/scripts/SaneMagic")
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()
    result = {"scope": "Lua behavioural parity with controlled game services; not native performance or in-game validation",
              "lupa_version": importlib.metadata.version("lupa"), "cases": []}
    for name, scenario in [("npc.lua", NPC_SCENARIO), ("daesumm_pn.lua", SUMMON_SCENARIO)]:
        baseline_path = args.baseline_dir / name
        if baseline_path.is_file():
            baseline_bytes = baseline_path.read_bytes()
        else:
            baseline_bytes = gzip.decompress((args.baseline_dir / (name + ".gz")).read_bytes())
        baseline_hash = hashlib.sha256(baseline_bytes).hexdigest()
        if baseline_hash != BASELINE_HASHES[name]:
            raise RuntimeError(f"Unexpected installed baseline for {name}: {baseline_hash}")
        original = baseline_bytes.decode("utf-8-sig")
        modified_bytes = (args.mod_dir / name).read_bytes()
        modified = modified_bytes.decode("utf-8-sig")
        for runtime in ["lupa.lua51", "lupa.luajit21"]:
            expected, old_counts = run(runtime, original, False, scenario)
            for enabled in [False, True]:
                actual, counts = run(runtime, modified, enabled, scenario)
                if actual != expected:
                    failures = [(i + 1, a, b) for i, (a, b) in enumerate(zip(expected, actual)) if a != b]
                    raise AssertionError(json.dumps({"file": name, "runtime": runtime, "native": enabled, "failures": failures}, indent=2))
                if enabled:
                    assert counts.get("source_query_reads" if name == "npc.lua" else "magnitude_query_reads", 0) > 0
                    assert counts.get("legacy_spell_reads" if name == "npc.lua" else "legacy_effect_gets", 0) == 0
                result["cases"].append({"file": name, "runtime": runtime, "native_queries": enabled,
                    "snapshots_compared": len(actual), "result": "PASS", "native_service_counters": counts,
                    "baseline_service_counters": old_counts, "baseline_sha256": baseline_hash,
                    "adapter_sha256": hashlib.sha256(modified_bytes).hexdigest()})
    result["result"] = "PASS"
    result["snapshot_comparisons"] = sum(case["snapshots_compared"] for case in result["cases"])
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"result": result["result"], "cases": len(result["cases"]), "snapshot_comparisons": result["snapshot_comparisons"]}))


if __name__ == "__main__":
    main()
