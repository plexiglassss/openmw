"""Require completed spatial production-header and real engine/API regressions."""
from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[3]
expected = {'SpatialDistance': 6, 'PositionRevision': 2, 'SpatialBindings': 1}
files = {}
for name in ('openmw-tests.xml', 'spatial-actor-off.xml', 'spatial-distance-off.xml', 'spatial-both-off.xml'):
    path = root / name
    counts = dict.fromkeys(expected, 0)
    for case in ET.parse(path).iter('testcase'):
        suite = case.get('classname')
        if suite not in expected:
            continue
        if (case.get('status') != 'run' or case.get('result') != 'completed'
                or case.find('failure') is not None or case.find('skipped') is not None):
            raise RuntimeError(f'Required spatial regression did not pass: {suite}.{case.get("name")}')
        counts[suite] += 1
    if counts != expected:
        raise RuntimeError(f'Missing required spatial regression: {name}: {counts}')
    files[name] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'tests': counts}
path = root / 'spatial-index-windows.json'
standalone = json.loads(path.read_text(encoding='utf-8-sig'))
checks = {'float_boundary', 'underflow', 'random_parity', 'extreme_float_parity',
          'fallbacks', 'mutation_order', 'reentrancy', 'storage_clear'}
if standalone.get('result') != 'PASS' or set(standalone.get('checks', [])) != checks:
    raise RuntimeError('Production spatial index or mutation traversal regressions missing')
files[path.name] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'checks': sorted(checks)}
result = {'result': 'PASS', 'files': files, 'in_game_measured': False,
          'scope': 'Production broad phase/traversal plus real native distance service, Lua API and reference epochs; no in-game combat outcome or FPS certification'}
(root / 'spatial-query-tests.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS: spatial core and real engine/API cases passed in all four comparison modes.')
