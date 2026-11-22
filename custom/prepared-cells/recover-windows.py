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
# Deterministic output of the ordinary Qt CMake path in failed run221;
# the source archive's only mutation was lupdate on seven TS files.
TRANSLATION_ORIGIN_ARTIFACT = 14734311108
TRANSLATION_ORIGIN_SHA256 = '1c5b9b3d6c3e6ef48abbcd6a10c558ec11a2882cf564acaa9e7916fe4445c1b5'
TRANSLATION_SHA256 = {
    "components_bg.qm": "1a1104d518e84733226b4755d22024951c1837eef4880b214ca53ccab060d2f1",
    "components_de.qm": "57c2b29b3a4c3cb7a1e64d37fdce2b08b8a90eb49560d5ec6d8e261384c8dbfe",
    "components_en.qm": "ae7be3a1e0712f997f96ea669a8a41f67e585c293c60f07db1e90973c9a61860",
    "components_fr.qm": "abaff4ab71c70cff93e0fc4fee7d7dc9d763fa5ea765db976f19f2e6b8a82de4",
    "components_pl.qm": "82af490ff981f7bcd0799f6c1d9acd607b91384ce0c9591d5c6ea745952b2cbb",
    "components_ru.qm": "53c67fb8baeca7dcd05df05908a4adac5f5580f83637a2fbe865f174ae32af20",
    "components_sv.qm": "c965e953596d17712bb4ec1d38e22f508bd6a06350f7f731523829a8d7b747d3",
    "launcher_bg.qm": "6253fa3e80d9e4f29ece3bf8f55af8b3fa0de112ab30ed604675627ff1ab3a27",
    "launcher_de.qm": "63bc7e408f74ddac5ebc170e30b2d145d5cc11a2477643c84639966b44ca0451",
    "launcher_en.qm": "4a2f4852cafbefa06d343eb6b5c73e233d36383f478f66be276df44516b1b381",
    "launcher_fr.qm": "079e6b1350d6b47adfbd7eb52a4fc64f25a0f09189d2c221ac0edd32c0a90888",
    "launcher_pl.qm": "a6db20b793cdb461ae112ffd67ab5da4ba621047bde1c48873b1f8434def4a9d",
    "launcher_ru.qm": "fea1bf05ceac8a7f6d1c6f1c1da0c536639ec0a96dfd7e63cdf27198f95a989d",
    "launcher_sv.qm": "55d0ef0068ca8878701c3bb5425ee58a8beae3d7e9d0e4a3c99808c56169174b",
    "qtbase_bg.qm": "e448ac9e3f16c29eb27af3012efe21052daa78fabfb34cd6dff2f69ee3bd3cdb",
    "qtbase_de.qm": "957823f398829213550d5a39ac2eff27f68c0fc2404b280acac461eacee0ede3",
    "qtbase_en.qm": "9959b510b15d18937848ad13007e30459d2e993c67e564badbfc18f935695c85",
    "qtbase_fr.qm": "314243a55c32679ad473fdfe48dc7d66e973ad0fc8bf2164480c58dddb50c1a7",
    "qtbase_pl.qm": "ce99032a3b0bf8abad758895cc22837088ead99fd2d2514e2d180693081cfe57",
    "qtbase_ru.qm": "3da6605668f9178d11a838c4515478084dcfb4f9cf22f99d7a92b492db9c224b",
    "qt_sv.qm": "4b4b6ff7fd237c9da0301b4946132e68653d15eb5faf38e4c5fbfebb12dd97f7",
    "wizard_bg.qm": "ae8b345ff9552304bcee05e99936792c4f48da4153f46ca48f50f6f9d6a85253",
    "wizard_de.qm": "63bc7e408f74ddac5ebc170e30b2d145d5cc11a2477643c84639966b44ca0451",
    "wizard_en.qm": "72aa5370e21f4eb6f0e83264e840d7a11ed68a93ff26e1321495abe03ea868de",
    "wizard_fr.qm": "a6d64aa3827532689492b704c4847b6bd7500f8636d274b79715075b3b1deb0b",
    "wizard_pl.qm": "2085f31da9a9f6f77d7fafa15d69686b76bc5b9b29b5b8b35e74ee34721890e2",
    "wizard_ru.qm": "ce90b6adf50c65fcd4b8081b802c564ffb64fe66b548edbf5fccb96629dce8c6",
    "wizard_sv.qm": "80542901ed2f48487e806b51a1f485957760d4b89764b375013c148282909093"
}
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


def resources(code):
    """Use the ordinary Qt generators without modifying the committed TS inputs."""
    verify_source_bytes()
    root = Path.cwd()
    qt = Path(cache(Path('build/CMakeCache.txt').read_text())['Qt6_DIR']).parents[2]
    executables = {name: qt / 'bin' / (name + '.exe') for name in ('lupdate', 'lrelease', 'qmake')}
    temporary = code / 'translation-inputs'
    temporary.mkdir(exist_ok=False)
    output = root / 'build/resources/translations'
    output.mkdir(parents=True, exist_ok=True)
    groups = {'components': ('components/contentselector', 'components/process'),
              'wizard': ('apps/wizard',), 'launcher': ('apps/launcher',)}
    inputs, generated, languages = {}, {}, set()
    for prefix, sources in groups.items():
        originals = sorted((root / 'files/lang').glob(prefix + '_*.ts'))
        require(len(originals) == 7, 'Unexpected translation source census')
        copies = []
        for source in originals:
            target = temporary / source.name
            shutil.copyfile(source, target)
            inputs[source.relative_to(root).as_posix()] = digest(source)
            copies.append(target)
            languages.add(source.stem.split('_', 1)[1])
        # CMake's translations target uses these same source groups and options.
        subprocess.run([str(executables['lupdate']), '-locations', 'none',
                        *[str(root / p) for p in sources], '-ts', *map(str, copies)],
                       cwd=root / 'apps/launcher', check=True)
        for source in copies:
            generated[source.name] = digest(source)
            subprocess.run([str(executables['lrelease']), '-silent', str(source),
                            '-qm', str(output / (source.stem + '.qm'))], check=True)
    qt_version = subprocess.check_output([str(executables['qmake']), '-query', 'QT_VERSION'], text=True).strip()
    require(qt_version == '6.6.3', 'Translation tool version differs')
    qt_translations = Path(subprocess.check_output([str(executables['qmake']), '-query',
                                                    'QT_INSTALL_TRANSLATIONS'], text=True).strip())
    qt_files = {}
    for language in sorted(languages):
        candidates = [qt_translations / ('qtbase_' + language + '.qm'), qt_translations / ('qt_' + language + '.qm')]
        source = next((p for p in candidates if p.is_file()), None)
        require(source is not None, 'Missing Qt translation: ' + language)
        shutil.copyfile(source, output / source.name)
        qt_files[source.name] = digest(source)
    outputs = {p.name: digest(p) for p in output.glob('*.qm')}
    require(outputs == TRANSLATION_SHA256, 'Generated translations differ from the ordinary Qt build')
    require(all(digest(root / p) == sha for p, sha in inputs.items()), 'Translation input source was modified')
    receipt = json.loads((code / 'receipt.json').read_text())
    receipt['translation_generation'] = {'mode': 'ordinary_Qt_generators_on_temporary_TS_copies', 'qt_version': qt_version,
        'comparison_origin_artifact': TRANSLATION_ORIGIN_ARTIFACT, 'comparison_origin_sha256': TRANSLATION_ORIGIN_SHA256,
        'original_ts_sha256': inputs, 'generated_ts_sha256': generated, 'qt_files_sha256': qt_files,
        'output_qm_sha256': outputs, 'tools_sha256': {n: digest(p) for n, p in executables.items()}}
    dump(code / 'receipt.json', receipt)
    verify_source_bytes()


def verify_source_bytes():
    head = git('rev-parse', 'HEAD')
    rows = subprocess.check_output(['git', 'ls-tree', '-r', '-z', COMPILED]).split(b'\0')
    changed = []
    checked = 0
    for row in rows:
        if not row:
            continue
        identity, raw_name = row.split(b'\t', 1)
        mode, kind, expected = identity.decode().split()
        name = raw_name.decode('utf-8')
        path = Path(name)
        require(kind == 'blob' and mode in ('100644', '100755'), 'Unsupported source entry: ' + name)
        actual = None
        if path.is_file() and not path.is_symlink():
            data = path.read_bytes()
            actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        if actual != expected:
            changed.append({'path': name, 'expected_blob': expected, 'actual_blob': actual})
        checked += 1
    audit = {'head': head, 'compiled_commit': COMPILED, 'tracked_files_checked': checked,
             'compiled_tree': git('rev-parse', COMPILED + '^{tree}'), 'byte_mismatches': changed,
             'index_changes': git('diff', '--cached', COMPILED, '--name-only').splitlines(),
             'git_worktree_diff_names': git('diff', COMPILED, '--name-only').splitlines()}
    dump('recovery-source-audit.json', audit)
    require(head == COMPILED and checked > 0 and not changed and not audit['index_changes'],
            'Compiled source identity/content changed: ' + json.dumps(audit))
    return audit


def finish(code):
    source_audit = verify_source_bytes()
    receipt = json.loads((code / 'receipt.json').read_text())
    receipt['final_source_audit'] = source_audit
    require(receipt['translation_generation']['output_qm_sha256'] == TRANSLATION_SHA256, 'Missing translation generation proof')
    for name, sha in TRANSLATION_SHA256.items():
        require(digest('install/resources/translations/' + name) == sha,
                'Installed generated translation differs: ' + name)
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
    parser.add_argument('phase', choices=('prepare', 'restore', 'resources', 'finish'))
    parser.add_argument('--code', type=Path, required=True)
    args = parser.parse_args()
    {'prepare': prepare, 'restore': restore, 'resources': resources, 'finish': finish}[args.phase](args.code.resolve())
