"""Execute production cell-reference merge bodies against lightweight fixtures.

Uses the real FormId, tuple helpers and transient index. Record/store/world
dependencies are mocked: this is not a native ESM parser or saved-game test.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'apps/openmw/mwworld/cellstore.cpp').read_text(encoding='utf-8')

def extract(start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]

parts = {
    'CONTEXT': extract('    struct CellRefLoadContext\n', '    template <typename X>'),
    'LOAD': extract('    template <typename X>\n    void CellRefList<X>::load(ESM::CellRef&',
                    '    static constexpr bool isESM4ActorRec'),
    'DISPATCH': extract('    template <typename RecordType, typename Callable>\n',
                        '    // helper function for forEachInternal'),
    'LOADREF': extract('    void CellStore::loadRef(ESM::CellRef&', '    void CellStore::loadState('),
}
fixture = (root / 'custom/performance-tests/content-reference-fixture.cpp.in').read_text(encoding='utf-8')
for name, body in parts.items():
    marker = '// @' + name + '@'
    assert fixture.count(marker) == 1
    fixture = fixture.replace(marker, body)

# Guard actual integration: context stays local and is shared with leased refs;
# ESM4, save restoration and runtime list mutation do not retain this index.
load = extract('    void CellStore::loadRefs()\n', '    bool CellStore::isExterior()')
assert 'CellRefLoadContext context;' in load and 'loadRefs(cell, context)' in load
content = extract('    void CellStore::loadRefs(const ESM::Cell&',
                  '    void CellStore::loadRefs(const ESM4::Cell&')
assert content.count('loadRef(ref, deleted, context);') == 2
assert 'LoadOnlyNotMoved' in content and 'cell.mMovedRefs' in content
assert 'cell.mLeasedRefs' in content

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    cpp = tmp / 'content-reference-fixture.cpp'
    cpp.write_text(fixture, encoding='utf-8')
    exe = tmp / ('content-reference-test.exe' if os.name == 'nt' else 'content-reference-test')
    if os.name == 'nt':
        compiler = shutil.which('cl')
        assert compiler, 'Run in an MSVC environment'
        argv = [compiler, '/nologo', '/std:c++20', '/EHsc', '/O2', '/W4', '/WX',
                f'/I{root}', str(cpp), f'/Fe:{exe}']
    else:
        argv = ['g++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                f'-I{root}', str(cpp), '-o', str(exe)]
        if os.environ.get('OPENMW_INDEX_SANITIZE') == '1':
            argv[2:3] = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    subprocess.run(argv, cwd=tmp, check=True)
    subprocess.run([str(exe)], cwd=tmp, check=True)
print('Content-reference production-body fixture passed (mocked records; no live-game claim)')
