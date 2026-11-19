"""Require completed native jobs regressions from the two fresh Windows test executables."""
import hashlib
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

components, engine, output = map(Path, sys.argv[1:])
required = {
    components: {'NativeJobPoolTest': 4, 'ActorNeighboursTest': 1, 'ResourceResourceSystem': 1},
    engine: {'NativeQueryServiceTest': 4, 'CellLookupTest': 3},
}
reports = {}
for path, suites in required.items():
    xml = ET.parse(path).getroot()
    cases = list(xml.iter('testcase'))
    if any(c.get('result') != 'completed' or c.get('status') != 'run' or c.find('failure') is not None for c in cases):
        raise ValueError(f'Incomplete or failing test in {path}')
    exercised = []
    for suite, minimum in suites.items():
        names = [c.get('name') for c in cases if c.get('classname') == suite
                 and (suite != 'ResourceResourceSystem' or c.get('name').startswith('nativePaging'))
                 and (suite != 'CellLookupTest' or c.get('name').startswith('multiType'))]
        if len(set(names)) < minimum:
            raise ValueError(f'Missing native-job tests in {path}: {suite} / {names}')
        exercised.extend(f'{suite}.{name}' for name in names)
    reports[path.name] = dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                              tests=len(cases), native_job_cases=exercised)
output.write_text(json.dumps(dict(result='PASS', executions=reports),indent=2)+'\n')
