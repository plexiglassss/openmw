"""Reject any reintroduction of the user-rejected post-load waiting stage."""
from pathlib import Path
import hashlib
import json

from reviewed_state import before_map_restore

root = Path(__file__).resolve().parents[2]

def get(name):
    return (root / name).read_text(encoding='utf-8')

def check(condition, message):
    if not condition:
        raise SystemExit(message)

def blob(text):
    data = text.encode('utf-8')
    return hashlib.sha1(b'blob ' + str(len(data)).encode('ascii') + b'\0' + data).hexdigest()

# Bind complete native files to their reviewed versions.
# StateManager preserves native-map dispatch and adds REC_DCLR beside REC_DCOU.
# Its exact hash and the removal proof below retain the entire prior loading,
# input and simulation implementation, including the earlier REC_LMRK case.
unchanged = {
    # Exact candidate engine with the worker pool constructed in prepareEngine and joined at shutdown.
    'apps/openmw/engine.cpp': 'a8bcc2819eb4f07c23162a20cfa7d682f8f346ca',
    'apps/openmw/mwgui/loadingscreen.cpp': '00f2a04d44b1df4456a316e44dd8959b048114b3',
    'apps/openmw/mwgui/loadingscreen.hpp': '268e676b011b99eccee41751f4df93e78e7dae9b',
    'apps/openmw/mwstate/statemanagerimp.cpp': '98b44cc7fd339a610f6257043a90cb95f18f2521',
}
for name, expected in unchanged.items():
    check(blob(get(name)) == expected, 'Original loading/input/simulation behavior changed: ' + name)
state = before_map_restore(get('apps/openmw/mwstate/statemanagerimp.cpp'))
added_case = '                case ESM::REC_DCLR:\n'
check(state.count(added_case) == 1 and blob(state.replace(added_case, '', 1)) == '026eaf0b580c46f2559b3b6d24a298a3322d5f55',
      'Cleared-history dispatcher differs beyond its one reviewed save-record case')
for name in ('components/loadinglistener/gameplayhandoff.hpp', 'custom/loading-repair/handoff-test.cpp'):
    check(not (root / name).exists(), 'Rejected waiting-stage implementation remains: ' + name)

world = get('apps/openmw/mwworld/worldmodel.cpp')
header = get('apps/openmw/mwworld/worldmodel.hpp')
check('prepareCellLookup' not in world + header, 'Extra pre-game preparation must not remain')
selection = world.split('if (useIndexedInactiveCellCandidates())', 1)[1].split('            else', 1)[0]
check('for (CellStore* cell : mUnindexedLookupCells)' not in selection,
      'Regression: indexed query still scans every unloaded cell')
check('mUnindexedFallbackCells.begin()' in selection, 'Exceptional candidates not included')
check('mUnindexedFallbackCells.clear();' in world, 'Clear must discard fallback membership')
check('mUnindexedFallbackCells.erase(cell);' in world, 'Loaded cells must leave fallback set')
check('mUnindexedFallbackCells.insert(cell);' in world, 'Runtime additions must remain searchable')
manifest = json.loads(get('loading-repair-source-manifest.json'))
changed = {name for name, value in manifest['files'].items() if value['before_blob'] != value['after_blob']}
check(changed == {'apps/openmw/mwworld/worldmodel.cpp', 'apps/openmw/mwworld/worldmodel.hpp'},
      'Only the two lookup implementation files may be changed by the build-time script')
print('PASS: saved-map hooks verified; previous loading and simulation source reproduced; lookup patch retained.')
print('These are source checks, not an in-game performance result.')
