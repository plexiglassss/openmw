"""Negative acceptance checks using executed XML, never synthetic passing receipts."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
for name in ('off', 'on', 'components', 'output'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
source = ET.parse(args.on)
checks = []
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    def run(label, mutate=None):
        tree = copy.deepcopy(source)
        if mutate:
            mutate(tree.getroot())
        xml = temp / 'on.xml'
        tree.write(xml)
        result = subprocess.run([sys.executable, str(Path(__file__).with_name('verify-tests.py')),
            '--off', str(args.off.resolve()), '--on', str(xml), '--components', str(args.components.resolve()),
            '--output', str(temp / 'receipt.json')], capture_output=True, text=True)
        if (result.returncode == 0) != (mutate is None):
            raise SystemExit(label + ': unexpected verifier result: ' + result.stdout + result.stderr)
        checks.append({'case': label, 'result': 'PASS', 'verifier_exit': result.returncode})
    def fixture(root):
        return root.find("./testsuite[@name='PreparedCellTest']")
    run('accept_executed_baseline')
    run('reject_duplicate_identity', lambda r: fixture(r).append(copy.deepcopy(fixture(r)[0])))
    run('reject_duplicate_property', lambda r: fixture(r)[0].find('properties').append(
        copy.deepcopy(fixture(r)[0].find('properties')[0])))
    run('reject_failed_extra_case', lambda r: fixture(r).append(ET.fromstring(
        '<testcase name="UnexpectedFailure" classname="PreparedCellTest" status="run" result="completed"><failure /></testcase>')))
    run('reject_inconsistent_failure_summary', lambda r: r.set('failures', '1'))
    run('reject_wrong_mode', lambda r: fixture(r)[0].find("./properties/property[@name='prepared_cells_enabled']").set('value', 'false'))
    run('reject_missing_case', lambda r: fixture(r).remove(fixture(r)[0]))
    run('reject_skipped_case', lambda r: ET.SubElement(fixture(r)[0], 'skipped'))
    run('reject_unrun_case', lambda r: fixture(r)[0].set('status', 'notrun'))
    def nonfinite(root):
        prop = next(p for p in root.iter('property') if p.get('name') == 'staged_owner_samples_ms')
        prop.set('value', ','.join(['nan'] + prop.get('value').split(',')[1:]))
    run('reject_nonfinite_timing', nonfinite)
args.output.write_text(json.dumps({'result':'PASS','checks':checks},indent=2)+'\n')
print('Passed',len(checks),'evidence-verifier checks')
