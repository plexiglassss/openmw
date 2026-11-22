"""Exercise the count correction and fail-closed binary recovery on real CI evidence."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--artifact', type=Path, required=True)
parser.add_argument('--native-evidence', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('recovery', Path(__file__).with_name('recover-windows.py'))
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)
checks = []


def reject(name, fn):
    try:
        fn()
    except (ValueError, KeyError):
        checks.append({'case': name, 'result': 'PASS'})
    else:
        raise AssertionError('Invalid input accepted: ' + name)


recovery.validate_archive(args.artifact)
checks.append({'case': 'accept_original_complete_binary_artifact', 'result': 'PASS'})
old = recovery.DIGEST
recovery.DIGEST = '0' * 64
reject('reject_wrong_archive_digest', lambda: recovery.validate_archive(args.artifact))
recovery.DIGEST = old
old = recovery.COMPILED
recovery.COMPILED = '0' * 40
reject('reject_wrong_compiled_source', lambda: recovery.validate_archive(args.artifact))
recovery.COMPILED = old
recovery.EXPECTED.add('missing-controls')
reject('reject_missing_required_executable', lambda: recovery.validate_archive(args.artifact))
recovery.EXPECTED.remove('missing-controls')
reject('reject_incomplete_config_evidence', lambda: recovery.cache('OPENMW_LTO_BUILD:BOOL=ON\n'))
for value in ('../openmw.exe', '/openmw.exe', 'C:/openmw.exe', 'build\\openmw.exe', 'build/./openmw.exe', 'build /openmw.exe'):
    reject('reject_unsafe_member_' + repr(value), lambda value=value: recovery.canonical(value))

verifier = Path(__file__).resolve().parents[1] / 'cell-streaming/verify-tests.py'
with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(args.native_evidence) as z:
    root = Path(tmp)
    for name in ('components-tests.xml', 'openmw-tests.xml', 'cell-packages-legacy.xml'):
        (root / name).write_bytes(z.read(name))

    def run(expected):
        result = subprocess.run([sys.executable, str(verifier), '--root', str(root)], capture_output=True, text=True)
        assert (result.returncode == 0) == expected, result.stdout + result.stderr

    run(True)
    checks.append({'case': 'accept_all_22_executed_preparation_tests', 'result': 'PASS'})
    path = root / 'components-tests.xml'
    original = path.read_bytes()
    tree = ET.fromstring(original)
    suite = next(s for s in tree if s.get('name') == 'AsyncPreparation')
    suite.remove(suite.find('testcase'))
    path.write_bytes(ET.tostring(tree))
    run(False)
    checks.append({'case': 'reject_missing_preparation_case', 'result': 'PASS'})
    tree = ET.fromstring(original)
    suite = next(s for s in tree if s.get('name') == 'AsyncPreparation')
    suite.find('testcase').set('status', 'notrun')
    path.write_bytes(ET.tostring(tree))
    run(False)
    checks.append({'case': 'reject_unexecuted_preparation_case', 'result': 'PASS'})
    tree = ET.fromstring(original)
    suite = next(s for s in tree if s.get('name') == 'AsyncPreparation')
    ET.SubElement(suite.find('testcase'), 'failure')
    path.write_bytes(ET.tostring(tree))
    run(False)
    checks.append({'case': 'reject_failed_preparation_case', 'result': 'PASS'})

with tempfile.TemporaryDirectory() as tmp:
    previous = Path.cwd()
    old_git = recovery.git
    old_redist = os.environ.get('VCToolsRedistDir')
    try:
        os.chdir(tmp)
        recovery.git = lambda *args: recovery.COMPILED if args == ('rev-parse', 'HEAD') else ''
        code = Path('code'); code.mkdir()
        Path('build').mkdir(); Path('install/verification').mkdir(parents=True)
        Path('redist/x64/Microsoft.VC143.CRT').mkdir(parents=True)
        os.environ['VCToolsRedistDir'] = str(Path('redist').resolve())
        names = [n + '.exe' for n in recovery.EXPECTED] + ['defaults.bin', 'defaults-cs.bin', 'qwindows.dll']
        files = {}
        for name in names:
            path = Path('build') / name; path.write_bytes(name.encode())
            files[path.as_posix()] = {'sha256': recovery.digest(path), 'bytes': path.stat().st_size}
            if name not in ('components-tests.exe', 'openmw-tests.exe', 'openmw-controls-tests.exe'):
                target = Path('install/platforms/qwindows.dll') if name == 'qwindows.dll' else Path('install') / name
                target.parent.mkdir(exist_ok=True); target.write_bytes(path.read_bytes())
        Path('build/CMakeCache.txt').write_text('new cache')
        (code / 'original-CMakeCache.txt').write_text('original cache')
        recovery.dump(code / 'receipt.json', {'restored_files': files})
        Path('redist/x64/Microsoft.VC143.CRT/vcruntime140.dll').write_bytes(b'runner CRT')
        Path('install/vcruntime140.dll').write_bytes(b'runner CRT')
        recovery.finish(code)
        checks.append({'case': 'accept_exact_installed_native_files_and_identified_CRT', 'result': 'PASS'})
        path = Path('install/platforms/qwindows.dll'); path.write_bytes(b'different plugin')
        reject('reject_changed_installed_plugin_with_build_unchanged', lambda: recovery.finish(code))
        path.write_bytes(Path('build/qwindows.dll').read_bytes())
        path = Path('install/vcruntime140.dll'); path.write_bytes(b'different CRT')
        reject('reject_changed_installed_CRT', lambda: recovery.finish(code))
        path.write_bytes(b'runner CRT')
        path = Path('install/unexpected.dll'); path.write_bytes(b'unidentified module')
        reject('reject_unidentified_native_module', lambda: recovery.finish(code))
    finally:
        os.chdir(previous)
        recovery.git = old_git
        if old_redist is None:
            os.environ.pop('VCToolsRedistDir', None)
        else:
            os.environ['VCToolsRedistDir'] = old_redist

args.output.write_text(json.dumps({'result': 'PASS', 'checks': checks}, indent=2) + '\n')
print('PASS:', len(checks), 'recovery/count-correction checks')
