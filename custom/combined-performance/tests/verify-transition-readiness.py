"""Require actual production scheduler, graphics queue and position query tests."""
from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET


root = Path(__file__).resolve().parents[3]
requirements = {
    'components-tests.xml': {'WorkQueueCancellation': 2, 'CompilePriority': 2},
    'openmw-tests.xml': {'ArrivalCancellation': 1, 'PositionSnapshots': 4, 'MagicEffectAccountingTest': 5},
    'previous-position-reads.xml': {'PositionSnapshots': 4},
}
files = {}
for name, expected in requirements.items():
    path = root / name
    tree = ET.parse(path)
    counts = {suite: 0 for suite in expected}
    for case in tree.iter('testcase'):
        suite = case.get('classname')
        if suite in counts:
            if (case.get('status') != 'run' or case.get('result') != 'completed'
                    or case.find('failure') is not None or case.find('skipped') is not None):
                raise RuntimeError(f'Required test did not pass: {suite}.{case.get("name")}')
            counts[suite] += 1
    if counts != expected:
        raise RuntimeError(f'Missing required regressions in {name}: {counts} != {expected}')
    files[name] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'tests': counts}
output = {'result': 'PASS', 'files': files, 'in_game_measured': False}
(root / 'transition-readiness-tests.json').write_text(json.dumps(output, indent=2) + '\n')
print('PASS: required arrival, compile priority and cached/uncached position tests executed.')
