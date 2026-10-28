"""Reject any reintroduction of the user-rejected post-load waiting stage."""
from pathlib import Path
import hashlib
import json

root = Path(__file__).resolve().parents[2]

def get(name):
    return (root / name).read_text(encoding='utf-8')

def check(condition, message):
    if not condition:
        raise SystemExit(message)

def blob(text):
    data = text.encode('utf-8')
    return hashlib.sha1(b'blob ' + str(len(data)).encode('ascii') + b'\0' + data).hexdigest()

# These complete native files must be byte-identical to the pre-handoff baseline.
# This verifies behavior restoration rather than just removal of the label.
unchanged = {
    'apps/openmw/engine.cpp': '569b7ea2871e6e756d3ce16a80b9e6cdf8b65964',
    'apps/openmw/mwgui/loadingscreen.cpp': '00f2a04d44b1df4456a316e44dd8959b048114b3',
    'apps/openmw/mwgui/loadingscreen.hpp': '268e676b011b99eccee41751f4df93e78e7dae9b',
    'apps/openmw/mwstate/statemanagerimp.cpp': '9c8621e46bce58a84e2c8501d93fad6e02e6bcf4',
}
for name, expected in unchanged.items():
    check(blob(get(name)) == expected, 'Original loading/input/simulation behavior changed: ' + name)
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
print('PASS: no added initializing stage; original loading/input/simulation code restored; lookup optimization retained.')
print('These are source checks, not an in-game performance result.')
