"""Require executed Windows tests for detached preparation and Lua setup parity."""
from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
requirements = {
    'components-tests.xml': {'SceneInstanceCacheTest': 6, 'InstancePreparation': 3,
                             'WorkQueueCancellation': 3, 'CellPackagesTest': 4},
    'cell-packages-legacy.xml': {'CellPackagesTest': 4},
    'openmw-tests.xml': {'PreloadDependenciesTest': 13},
}
files = {}
for name, expected in requirements.items():
    path = root / name
    tree = ET.parse(path)
    counts = {suite: 0 for suite in expected}
    actual_scene_manager = False
    for case in tree.iter('testcase'):
        suite = case.get('classname')
        scene_manager = (suite == 'ResourceResourceSystem'
                         and case.get('name') == 'preparedInstanceIsConsumedOnceAndSettingsInvalidateIt')
        if suite in expected or scene_manager:
            assert case.get('status') == 'run' and case.get('result') == 'completed', case.attrib
            assert case.find('failure') is None and case.find('skipped') is None, case.attrib
            if suite in counts:
                counts[suite] += 1
            actual_scene_manager |= scene_manager
    assert counts == expected, (name, counts, expected)
    if name == 'components-tests.xml':
        assert actual_scene_manager, 'Missing executed SceneManager integration regression'
    files[name] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                   'tests': counts, 'scene_manager_integration': actual_scene_manager}
output = {'result': 'PASS', 'files': files, 'in_game_measured': False}
(root / 'cell-streaming-tests.json').write_text(json.dumps(output, indent=2) + '\n')
print('PASS: cell preparation, ownership, cancellation, clone budget and Lua isolation/parity tests executed.')
