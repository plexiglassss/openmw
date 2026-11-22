"""Reject damaged staged-arrival evidence using actual executed test XML."""
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
sources = {name: ET.parse(getattr(args, name)) for name in ('off', 'on', 'components')}
checks = []
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)

    def run(label, target=None, mutate=None):
        trees = copy.deepcopy(sources)
        if mutate:
            mutate(trees[target].getroot())
        command = [sys.executable, str(Path(__file__).with_name('verify-staged.py'))]
        for name, tree in trees.items():
            path = temp / (name + '.xml')
            tree.write(path)
            command.extend(['--' + name, str(path)])
        result = subprocess.run(command + ['--output', str(temp / 'receipt.json')],
                                capture_output=True, text=True)
        if (result.returncode == 0) != (mutate is None):
            raise SystemExit(label + ': unexpected verifier result: ' + result.stdout + result.stderr)
        checks.append({'case': label, 'result': 'PASS', 'verifier_exit': result.returncode})

    def suite(root, name='StagedArrival'):
        return root.find("./testsuite[@name='" + name + "']")

    def wrong_mode(root, value):
        suite(root)[0].find("./properties/property[@name='staged_arrival_enabled']").set('value', value)

    def skip(root):
        case = suite(root)[0]
        ET.SubElement(case, 'skipped')
        case.set('result', 'skipped')
        root.set('skipped', '1')
        suite(root).set('skipped', '1')

    run('accept_actual_execution')
    run('reject_wrong_on_mode', 'on', lambda r: wrong_mode(r, 'false'))
    run('reject_wrong_off_mode', 'off', lambda r: wrong_mode(r, 'true'))
    run('reject_missing_required_case', 'on', lambda r: suite(r).remove(suite(r)[0]))
    run('reject_missing_real_terrain_fixture', 'on', lambda r: r.remove(suite(r, 'StagedTerrain')))
    run('reject_missing_cancellation_fixture', 'on', lambda r: r.remove(suite(r, 'ArrivalCancellation')))
    run('reject_skipped_case_with_consistent_summary', 'on', skip)
    run('reject_unrun_case', 'on', lambda r: suite(r)[0].set('status', 'notrun'))
    run('reject_duplicate_identity', 'on', lambda r: suite(r).append(copy.deepcopy(suite(r)[0])))
    run('reject_duplicate_property', 'on', lambda r: suite(r)[0].find('properties').append(
        copy.deepcopy(suite(r)[0].find('properties')[0])))
    run('reject_missing_component_case', 'components', lambda r: suite(r).remove(suite(r)[0]))
    run('reject_inconsistent_failure_summary', 'on', lambda r: r.set('failures', '1'))

args.output.write_text(json.dumps({'result': 'PASS', 'checks': checks}, indent=2) + '\n')
print('Passed', len(checks), 'staged evidence-verifier checks')
