"""Bind the loading-progress optimization to the accepted visual/activation baseline.

This source-preservation check is additional to native graphics/readiness tests;
it does not establish visual parity or performance on the user's installed mods.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = '8f466b0b6c736c12d7f5a76eebf6cf46434a0017'


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
    subprocess.run(['git', 'merge-base', '--is-ancestor', BASE, 'HEAD'], cwd=ROOT, check=True)
    changes = set(git('diff', '--name-only', BASE, '--', 'apps', 'components', 'files',
                      'CMakeLists.txt', 'cmake').decode().splitlines())
    expected = {'apps/openmw/mwgui/loadingscreen.cpp', 'components/loadinglistener/reporter.cpp',
                'components/loadinglistener/reporter.hpp'}
    if changes != expected:
        raise RuntimeError(f'Unexpected production changes: {sorted(changes ^ expected)}')
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
        'result': 'PASS', 'baseline': BASE, 'commit': git('rev-parse', 'HEAD').decode().strip(),
        'production_changes': sorted(changes), 'unchanged_loading_methods': unchanged,
        'admitted_graphics_service_unchanged': True,
        'all_other_tracked_apps_components_resources_and_build_definitions_unchanged': True,
        'candidate_sha256': {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sorted(changes)},
        'limitations': 'Exact source preservation only; native graphics/readiness tests and user gameplay are separate.',
    }
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: only progress UI and Reporter production paths changed; scene activation, view distance, resources, readiness and admitted graphics work preserved')


if __name__ == '__main__':
    main()
