"""Execute actual query and existing queue against the prior production handler.
Native cell/object APIs are fixtures; no installed-game timing is asserted.
"""
import argparse
import hashlib
import importlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
LUA = ROOT / 'Quest Guider Lite - Incremental Nearby Queries/scripts/quest_guider_lite'
BASE = HERE / 'fixtures/parallel-main.lua'
base = BASE.read_text()
helper = base[base.index('local function forEachSupportedGiver'):base.index('local function onInit')]
start = base.index('["QGL:getQuestsNearby"] = ') + len('["QGL:getQuestsNearby"] = ')
handler = base[start:base.index('\n        ["QGL:registerActorDeath"]', start)].strip().removesuffix(',')

def setup(runtime):
    lua = importlib.import_module('lupa.' + runtime).LuaRuntime(unpack_returned_tuples=True)
    g = lua.globals()
    compile_lua = lua.eval('function(s) assert((loadstring or load)(s)) end')
    for path in LUA.rglob('*.lua'):
        compile_lua(path.read_text())
    provenance = json.loads((ROOT/'provenance.json').read_text())
    assert hashlib.sha256(BASE.read_bytes()).hexdigest() == provenance['base_main_sha256']
    assert hashlib.sha256((HERE/'fixtures/indexedQueue.lua').read_bytes()).hexdigest() == provenance['queue_sha256']
    for name,digest in provenance['retained_helpers'].items():
        assert hashlib.sha256((LUA/name).read_bytes()).hexdigest() == digest
    g.QUEUE_SOURCE = (HERE/'fixtures/indexedQueue.lua').read_text()
    g.QUERY_SOURCE = (LUA/'storage/nearbyQueries.lua').read_text()
    g.LEGACY_HELPER, g.LEGACY_HANDLER = helper, handler
    return lua, lua.execute((HERE/'harness.lua').read_text())

def ids(message):
    return {value for _, value in message.data.diaIds.items()}

def run(runtime):
    checks = []
    for exterior in (False, True):
        lua,e = setup(runtime); e.fixture(350, exterior)
        data = e.request('menu', 'parity')
        e.queue.update()
        assert len(e.messages) == 0, 'partial result published'
        slices = e.finish()
        assert slices > 1 and len(e.messages) == 1
        result = e.messages[1]
        assert result.name == 'QGL:questsNearby' and result.data.requestId == 'parity'
        assert result.data.extra == 'preserved' and data.diaIds is None
        e.baseline(lua.table_from({'player':e.player,'menuId':'legacy'}))
        assert ids(result) == ids(e.messages[2]), 'legacy result set changed'
        assert ('only_reached_when_b_is_root' in ids(result)) == exterior
    checks.append('Dense interior/exterior results match original handler; shared/cyclic destinations preserve depth')

    lua,e = setup(runtime); e.fixture(600, True); e.request('menu','old'); e.queue.update()
    e.request('menu','new'); e.finish()
    assert len(e.messages)==1 and e.messages[1].data.requestId=='new'
    checks.append('Superseded same-menu request publishes only the new complete result')
    lua,e = setup(runtime); e.fixture(150, True); e.request('one','one'); e.request('two','two'); e.finish()
    assert {e.messages[i].data.requestId for i in (1,2)} == {'one','two'}
    checks.append('Independent menus retain separate responses and payloads')

    lua,e = setup(runtime); a,b,c = e.fixture(600, True); e.request(); e.queue.update()
    e.player.cell=c; e.finish(); assert len(e.messages)==0
    checks.append('Moving to another cell cancels a suspended response')
    lua,e = setup(runtime); e.fixture(600, True); e.request(); e.queue.update(); e.queue.clear(); e.finish()
    assert len(e.messages)==0
    checks.append('Existing load/clear path drops all suspended state')

    lua,e = setup(runtime); e.fixture(600, True); e.request(); e.queue.update()
    e.revision='_new'; e.queue.invalidate(); e.finish()
    assert len(e.messages)==1 and all(value.endswith('_new') for value in ids(e.messages[1]))
    checks.append('Data-generation changes restart privately without mixed old/new output')
    lua,e = setup(runtime); e.fixture(300, True); e.ready=False; e.request(); e.queue.update()
    assert len(e.messages)==0 and e.queue.getStats().pending==1
    e.ready=True; e.finish(); assert len(e.messages)==1
    checks.append('Initialization wait resumes on the existing queue')

    lua,e = setup(runtime); e.fixture(600, True); e.request(); e.queue.update()
    # Last object has not been consumed yet when the first bounded slice returns.
    e.objects[len(e.objects)].valid=False; e.finish()
    e.baseline(lua.table_from({'player':e.player,'menuId':'legacy'}))
    assert ids(e.messages[1]) == ids(e.messages[2])
    checks.append('Removed references are checked when resumed')

    patched=(LUA/'main.lua').read_text()
    assert '["QGL:getQuestsNearby"] = nearbyQueries.request,' in patched
    assert 'indexedQueue.update(dt)' in patched and 'ranking.update()' in patched and 'indexedQueue.clear()' in patched
    # The only main-script changes are dependency construction and replacing this UI handler.
    restored=patched.replace("local nearbyQueries = require('scripts.quest_guider_lite.storage.nearbyQueries').new(\n    indexedQueue, world, types, protectedDoor, questLib, dataHandler)\n\n",'')
    restored=restored.replace('["QGL:getQuestsNearby"] = nearbyQueries.request,','["QGL:getQuestsNearby"] = '+handler+',')
    assert restored == base, 'unrelated gameplay code changed'
    checks.append('Main-script binding preserves every other handler and parallel sorting (normalized line endings)')
    return {'runtime':runtime,'checks':checks,'passed':len(checks)}

if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--output',type=Path,required=True); args=parser.parse_args()
    results=[run(runtime) for runtime in ('lua51','lua54','luajit21')]
    sources=[BASE,LUA/'main.lua',LUA/'storage/nearbyQueries.lua',HERE/'fixtures/indexedQueue.lua',HERE/'harness.lua']
    args.output.write_text(json.dumps({'result':'PASS','results':results,'sha256':{str(p.relative_to(ROOT.parent.parent)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},'installed_game_tested':False},indent=2)+'\n')
    print('Nearby query checks passed in Lua 5.1, Lua 5.4 and LuaJIT 2.1')
