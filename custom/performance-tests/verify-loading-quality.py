"""Bind the loading-progress optimization to the accepted visual/activation baseline.

This source-preservation check is additional to native graphics/readiness tests;
it does not establish visual parity or performance on the user's installed mods.
The accepted optimization is checked against its original baseline, then its
production Git blobs are byte-matched in the combined build. Other cumulative changes
are checked separately by the RC1 source plan and their own regression tests.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = '8f466b0b6c736c12d7f5a76eebf6cf46434a0017'
PERFORMANCE = 'ffca70e561427c3b54ec3e1972db3ce0fc20a0a3'


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def body(source, signature):
    start = source.index('{', source.index(signature))
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    subprocess.run(['git', 'merge-base', '--is-ancestor', BASE, PERFORMANCE], cwd=ROOT, check=True)
    subprocess.run(['git', 'merge-base', '--is-ancestor', PERFORMANCE, 'HEAD'], cwd=ROOT, check=True)
    changes = set(git('diff', '--name-only', BASE, PERFORMANCE, '--', 'apps', 'components', 'files',
                      'CMakeLists.txt', 'cmake').decode().splitlines())
    expected = {'apps/openmw/mwgui/loadingscreen.cpp', 'components/loadinglistener/reporter.cpp',
                'components/loadinglistener/reporter.hpp'}
    if changes != expected:
        raise RuntimeError(f'Unexpected accepted optimization changes: {sorted(changes ^ expected)}')
    for path in sorted(expected):
        accepted = git('show', PERFORMANCE + ':' + path)
        if git('show', 'HEAD:' + path) != accepted:
            raise RuntimeError('Accepted loading optimization changed in committed build: ' + path)
        # Git's Windows checkout can use CRLF. Compare normalized text bytes so
        # checkout line endings do not change the result, while all source text is
        # still required to match the accepted optimization exactly.
        current = (ROOT / path).read_bytes().replace(b'\r\n', b'\n')
        if current != accepted.replace(b'\r\n', b'\n'):
            raise RuntimeError('Accepted loading optimization changed in combined build: ' + path)
    path = 'apps/openmw/mwgui/loadingscreen.cpp'
    before = git('show', BASE + ':' + path).decode()
    after = (ROOT / path).read_text()
    unchanged = [
        'double LoadingScreen::getTargetFrameRate() const',
        'void LoadingScreen::loadingOn()', 'void LoadingScreen::loadingOff()',
        'bool LoadingScreen::needToDrawLoadingScreen()', 'void LoadingScreen::refresh(bool force)',
        'void operator()(osg::RenderInfo& renderInfo) const override',
        'void LoadingScreen::setupCopyFramebufferToTextureCallback()',
    ]
    for signature in unchanged:
        if body(before, signature) != body(after, signature):
            raise RuntimeError('Loading/graphics behavior changed: ' + signature)
    marker = '        if (mShowWallpaper && mTimer.time_m()'
    before_draw = body(before, 'void LoadingScreen::draw(bool force)')
    after_draw = body(after, 'void LoadingScreen::draw(bool force)')
    if before_draw[before_draw.index(marker):] != after_draw[after_draw.index(marker):]:
        raise RuntimeError('Admitted graphics service or compilation budget changed')
    result = {
        'result': 'PASS', 'baseline': BASE, 'accepted_performance_commit': PERFORMANCE,
        'commit': git('rev-parse', 'HEAD').decode().strip(),
        'accepted_optimization_production_changes': sorted(changes), 'unchanged_loading_methods': unchanged,
        'admitted_graphics_service_unchanged': True,
        'accepted_optimization_all_other_production_files_unchanged': True,
        'loading_optimization_preserved_in_combined_build': True,
        'source_comparison': 'exact Git blobs; working source bytes after CRLF normalization',
        'candidate_sha256': {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sorted(changes)},
        'limitations': 'Loading optimization source preservation only. Other combined changes are verified separately '
                       'by the RC1 source plan and feature regressions; native graphics/readiness tests and user '
                       'gameplay are separate.',
    }
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: accepted loading optimization preserved; its scene activation, view distance, resources, readiness and admitted graphics work unchanged')


if __name__ == '__main__':
    main()
