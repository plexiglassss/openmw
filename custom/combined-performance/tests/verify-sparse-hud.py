"""Compare changed-widget and previous full updates under the same Windows executable."""
from pathlib import Path
from xml.etree import ElementTree
import json

def read(name):
    result = {}
    for case in ElementTree.parse(name).getroot().iter('testcase'):
        if case.get('classname') != 'LuaUiLayoutTest' or case.get('name') not in (
                'SparsePropertyRequestsMergeAndPreserveFullRefresh', 'BatchedHudPropertiesBenchmark'):
            continue
        assert case.get('result') == 'completed' and case.find('failure') is None
        result[case.get('name')] = {p.get('name'): p.get('value') for p in case.findall('./properties/property')}
    assert len(result) == 2
    return result

current = read('openmw-tests.xml')
previous = read('previous-hud-tests.xml')
for key in current:
    assert current[key]['layout_snapshots'] and current[key]['layout_snapshots'] == previous[key]['layout_snapshots']
benchmark = 'BatchedHudPropertiesBenchmark'
modes = {name: {k: int(cases[benchmark][k]) for k in ('median_refresh_ns', 'applied_widgets', 'rows')}
         for name, cases in [('current', current), ('previous', previous)]}
assert modes['current']['applied_widgets'] == 2 and modes['previous']['applied_widgets'] == 129
assert min(row['median_refresh_ns'] for row in modes.values()) > 0
out = {'result': 'PASS', 'modes': modes, 'identical_native_snapshots': sorted(current),
       'scope': 'Two changed widgets in a 32-row native HUD. Gameplay FPS requires a player capture.'}
Path('sparse-hud-tests.json').write_text(json.dumps(out, indent=2) + '\n', encoding='utf-8')
print(out)
