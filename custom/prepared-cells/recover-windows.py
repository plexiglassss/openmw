"""One-off recovery of exact compiled binaries; all tests and package checks rerun.

The checkout and package identities remain the original compiled commit. This
helper and the corrected test-count verifier come from the separate CI head.
No diagnostic archive is accepted as a ready distribution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import urllib.error
import urllib.parse
import urllib.request
import zipfile

COMPILED = 'f6b6bc529d5fc0f53ddb4fbe2c8f1ac68efec160'
RUN = 44961793194
ARTIFACT = 14732787779
BYTES = 81365741
DIGEST = '7772f267a187d463738196bba812f47ced71276125cf04f07b5bb44f56f6610c'
REPOSITORY = 'plexiglassss/openmw'
ALLOWED = {'.github/workflows/push.yml', 'custom/rc1/source-plan.json',
           'custom/cell-streaming/verify-tests.py', 'custom/prepared-cells/recover-windows.py',
           'custom/prepared-cells/test-recovery.py'}
EXPECTED = {'openmw', 'openmw-launcher', 'openmw-wizard', 'openmw-iniimporter',
            'openmw-navmeshtool', 'openmw-cs', 'openmw-essimporter', 'bsatool',
            'esmtool', 'niftest', 'openmw-bulletobjecttool', 'components-tests',
            'openmw-tests', 'openmw-controls-tests'}
CACHE_KEYS = ('CMAKE_BUILD_TYPE', 'CMAKE_CXX_FLAGS_RELEASE', 'CMAKE_EXE_LINKER_FLAGS_RELEASE',
              'OPENMW_LTO_BUILD', 'CMAKE_GENERATOR', 'VCPKG_TARGET_TRIPLET',
              'LuaJit_LIBRARY', 'Qt6_DIR', 'CMAKE_TOOLCHAIN_FILE')


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


def dump(path, data):
    Path(path).write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def canonical(name):
    p = PurePosixPath(name)
    require(bool(name) and not p.is_absolute() and '..' not in p.parts
            and '\\' not in name and ':' not in name and p.as_posix() == name
            and all(part.rstrip(' .') == part for part in p.parts), 'Unsafe archive path: ' + name)


def validate_archive(path):
    require(Path(path).stat().st_size == BYTES and digest(path) == DIGEST,
            'Original compiled artifact digest/size differs')
    with zipfile.ZipFile(path) as z:
        names = [i.filename for i in z.infolist() if not i.is_dir()]
        require(len(names) == len(set(n.casefold() for n in names)), 'Duplicate archive members')
        for name in names:
            canonical(name)
        m = json.loads(z.read('UNVERIFIED-MANIFEST.json').decode('utf-8-sig'))
        require(m['commit'] == COMPILED and int(m['run_id']) == RUN and m['run_attempt'] == '1'
                and not m['missing_binaries'] and m['not_a_distribution'] is True
                and m['status'] == 'UNVERIFIED_FAILED_RUN_DIAGNOSTICS_ONLY', 'Wrong original manifest')
        rows = {r['path']: r for r in m['files']}
        require(len(rows) == len(m['files']) and set(rows) | {'UNVERIFIED-MANIFEST.json'} == set(names),
                'Original manifest census differs')
        for name, row in rows.items():
            require(len(z.read(name)) == row['bytes'] and hashlib.sha256(z.read(name)).hexdigest() == row['sha256'],
                    'Original member differs: ' + name)
        require({n for n in rows if n.startswith('build/') and n.endswith('.exe')}
                == {'build/' + n + '.exe' for n in EXPECTED}, 'Original application census differs')
        for name in ('rc1-source-manifest.json', 'rc1-build-source-manifest.json'):
            source = json.loads(z.read(name).decode('utf-8-sig'))
            require(source['result'] == 'PASS' and source['commit'] == COMPILED
                    and source['preserved_ancestry_verified'], 'Original compiled source proof differs')
        return m


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def request(path):
    headers = {'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
               'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'}
    return urllib.request.Request('https://api.github.com/repos/' + REPOSITORY + path, headers=headers)


def download(destination):
    with urllib.request.urlopen(request('/actions/artifacts/' + str(ARTIFACT)), timeout=60) as response:
        meta = json.load(response)
    require(meta['id'] == ARTIFACT and meta['workflow_run']['id'] == RUN
            and meta['workflow_run']['head_sha'] == COMPILED and not meta['expired']
            and meta['name'] == 'UNVERIFIED-compiled-binaries-' + COMPILED
            and meta['size_in_bytes'] == BYTES and meta['digest'] == 'sha256:' + DIGEST,
            'GitHub artifact metadata differs')
    opener = urllib.request.build_opener(NoRedirect)
    try:
        opener.open(request('/actions/artifacts/' + str(ARTIFACT) + '/zip'), timeout=60)
        raise ValueError('Expected GitHub artifact redirect')
    except urllib.error.HTTPError as error:
        require(error.code == 302, 'Unexpected artifact response')
        url = error.headers['Location']
    require(urllib.parse.urlparse(url).scheme == 'https', 'Insecure artifact redirect')
    # The GitHub token is never forwarded to the artifact storage host.
    with urllib.request.urlopen(url, timeout=120) as response, Path(destination).open('wb') as output:
        shutil.copyfileobj(response, output)
    return meta


def prepare(code):
    head = git('rev-parse', 'HEAD')
    require(head == os.environ['PREPARED_VERIFIER_COMMIT'] and head != COMPILED,
            'Wrong recovery verification checkout')
    require(not git('diff', 'HEAD', '--name-only'), 'Tracked verification source is modified')
    subprocess.run(['git', 'merge-base', '--is-ancestor', COMPILED, head], check=True)
    changed = set(git('diff', '--name-only', COMPILED, head).splitlines())
    require(changed == ALLOWED, 'Recovery changed files outside the exact verification-only contract')
    code.mkdir(parents=True, exist_ok=False)
    for source, target in (('custom/prepared-cells/recover-windows.py', 'recover-windows.py'),
                           ('custom/cell-streaming/verify-tests.py', 'verify-tests.py')):
        shutil.copyfile(source, code / target)
    receipt = {'status': 'PENDING_TESTS_AND_PACKAGE_VALIDATION', 'compiled_commit': COMPILED,
               'verification_commit': head, 'recovery_run_id': int(os.environ['GITHUB_RUN_ID']),
               'recovery_run_attempt': int(os.environ['GITHUB_RUN_ATTEMPT']),
               'source_run_id': RUN, 'source_artifact_id': ARTIFACT,
               'source_artifact_sha256': DIGEST, 'source_artifact_bytes': BYTES,
               'verification_only_changes': {p: {'sha256': digest(p), 'blob': git('rev-parse', head + ':' + p)}
                                             for p in sorted(changed)},
               'all_native_build_inputs_unchanged': True,
               'tests_imported_from_failed_run': False}
    dump(code / 'receipt.json', receipt)
    subprocess.run(['git', 'checkout', '--detach', COMPILED], check=True)
    require(not git('diff', 'HEAD', '--name-only'), 'Compiled source checkout is modified')


def cache(data):
    values = {}
    for line in data.splitlines():
        if '=' in line and ':' in line.split('=', 1)[0]:
            key = line.split(':', 1)[0]
            if key in CACHE_KEYS:
                values[key] = line.split('=', 1)[1].replace('\\', '/')
    require(set(values) == set(CACHE_KEYS), 'Incomplete build configuration evidence')
    return values


def restore(code):
    require(git('rev-parse', 'HEAD') == COMPILED and not git('diff', 'HEAD', '--name-only'),
            'Not the pristine compiled source')
    archive = code / 'compiled.zip'
    metadata = download(archive)
    manifest = validate_archive(archive)
    with zipfile.ZipFile(archive) as z:
        require(cache(z.read('build/CMakeCache.txt').decode()) == cache(Path('build/CMakeCache.txt').read_text()),
                'Native build configuration changed')
        (code / 'original-CMakeCache.txt').write_bytes(z.read('build/CMakeCache.txt'))
        source = json.loads(z.read('rc1-build-source-manifest.json').decode('utf-8-sig'))
        require(source['source_plan_sha256'] == digest('custom/rc1/source-plan.json'),
                'Compiled source plan differs')
        # Do not import old tests, receipts, paths or CMake build descriptions.
        restored = {}
        for row in manifest['files']:
            name = row['path']
            if name.startswith('build/') and Path(name).suffix.lower() in ('.exe', '.dll', '.bin'):
                path = Path(name)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(z.read(name))
                restored[name] = {'sha256': row['sha256'], 'bytes': row['bytes']}
        require('build/defaults.bin' in restored and 'build/defaults-cs.bin' in restored, 'Missing compiled defaults')
    receipt = json.loads((code / 'receipt.json').read_text())
    receipt.update({'restored_files': restored, 'original_manifest': manifest,
                    'github_artifact_metadata': metadata,
                    'build_configuration': cache(Path('build/CMakeCache.txt').read_text())})
    dump(code / 'receipt.json', receipt)
    print('Restored exact compiled applications and dependencies; all native tests still required.')


def finish(code):
    require(git('rev-parse', 'HEAD') == COMPILED and not git('diff', 'HEAD', '--name-only'),
            'Compiled source changed during verification')
    receipt = json.loads((code / 'receipt.json').read_text())
    for name, row in receipt['restored_files'].items():
        require(digest(name) == row['sha256'] and Path(name).stat().st_size == row['bytes'],
                'A restored native binary changed: ' + name)
    for name in EXPECTED - {'components-tests', 'openmw-tests', 'openmw-controls-tests'}:
        require(digest('install/' + name + '.exe') == receipt['restored_files']['build/' + name + '.exe']['sha256'],
                'Packaged binary differs from recovered binary: ' + name)
    origin_names = {Path(p).name.casefold(): row for p, row in receipt['restored_files'].items()}
    require(len(origin_names) == len(receipt['restored_files']), 'Ambiguous recovered binary names')
    package_files = {}
    crt = Path(os.environ['VCToolsRedistDir']) / 'x64/Microsoft.VC143.CRT'
    for path in Path('install').rglob('*'):
        if not path.is_file() or path.suffix.lower() not in ('.exe', '.dll', '.bin'):
            continue
        relative = path.relative_to('install').as_posix()
        sha = digest(path)
        row = origin_names.get(path.name.casefold())
        if row is not None:
            require(sha == row['sha256'] and path.stat().st_size == row['bytes'],
                    'Packaged native dependency/default differs: ' + relative)
            basis = 'exact_original_compiled_artifact'
        elif path.parent == Path('install') and path.suffix.lower() == '.dll' and (crt / path.name).is_file():
            require(sha == digest(crt / path.name), 'Packaged CRT differs from runner redistributable')
            basis = 'current_runner_Visual_Studio_CRT_exercised_by_distribution_startup'
        else:
            require(relative == 'redist/vc_redist.x64.exe', 'Unexpected additional native payload: ' + relative)
            basis = 'Microsoft_signed_redistributable_verified_by_stage_msvc_runtime'
        package_files[relative] = {'sha256': sha, 'bytes': path.stat().st_size, 'basis': basis}
    receipt['packaged_native_files'] = package_files
    evidence = Path('install/verification/recovery')
    evidence.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(code / 'original-CMakeCache.txt', evidence / 'original-CMakeCache.txt')
    shutil.copyfile('build/CMakeCache.txt', evidence / 'regenerated-CMakeCache.txt')
    receipt['cmake_evidence'] = {p.name: digest(p) for p in evidence.iterdir()}
    receipt.update({'status': 'NATIVE_TESTS_AND_DISTRIBUTION_VALIDATED',
                    'meaning': 'Same f6 compiled engine; separate verification-only CI head. No gameplay result.'})
    dump('install/verification/COMPILED-BINARY-RECOVERY.json', receipt)



if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('phase', choices=('prepare', 'restore', 'finish'))
    parser.add_argument('--code', type=Path, required=True)
    args = parser.parse_args()
    {'prepare': prepare, 'restore': restore, 'finish': finish}[args.phase](args.code.resolve())
