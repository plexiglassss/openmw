"""Author-side, exact-baseline source edits. Never run on a user's installation.
CI applies only the independent lookup optimization. The rejected post-load
waiting stage and extra preparation at the end of loading are not applied.
CI archives the complete resulting diff.
All source blobs and replacement counts are checked before any file is written.
"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
EXPECTED = {
    'apps/openmw/engine.cpp': '569b7ea2871e6e756d3ce16a80b9e6cdf8b65964',
    'apps/openmw/mwgui/loadingscreen.cpp': '00f2a04d44b1df4456a316e44dd8959b048114b3',
    'apps/openmw/mwgui/loadingscreen.hpp': '268e676b011b99eccee41751f4df93e78e7dae9b',
    'apps/openmw/mwstate/statemanagerimp.cpp': '9c8621e46bce58a84e2c8501d93fad6e02e6bcf4',
    'apps/openmw/mwworld/worldmodel.cpp': 'd0e9d1c61392dbfa4983dcb28bdb65d35d3dfe35',
    'apps/openmw/mwworld/worldmodel.hpp': '233ca8ff1665323ca6efd96454c4bd3cd8ca88fd',
}

def blob(text):
    data = text.encode('utf-8')
    return hashlib.sha1(b'blob ' + str(len(data)).encode('ascii') + b'\0' + data).hexdigest()

sources = {}
for name, sha in EXPECTED.items():
    text = (ROOT / name).read_text(encoding='utf-8')
    if blob(text) != sha:
        raise SystemExit(f'Refusing unknown baseline for {name}: {blob(text)} != {sha}')
    sources[name] = text

edits = []
def replace(name, old, new):
    text = sources[name]
    if text.count(old) != 1:
        raise SystemExit(f'Expected exactly one anchor in {name}: {old[:100]!r}; got {text.count(old)}')
    sources[name] = text.replace(old, new, 1)
    edits.append({'file': name, 'anchor': old[:90]})

header = 'apps/openmw/mwworld/worldmodel.hpp'
replace(header, '        mutable LookupCells mUnindexedLookupCells;',
        '''        mutable LookupCells mUnindexedLookupCells;
        // Only exceptional unloaded cells require exhaustive runtime lookup.
        // Membership is refreshed on dirty notifications, not on each query.
        mutable LookupCells mUnindexedFallbackCells;''')
world = 'apps/openmw/mwworld/worldmodel.cpp'
replace(world, '    mUnindexedLookupCells.clear();',
        '    mUnindexedLookupCells.clear();\n    mUnindexedFallbackCells.clear();')
replace(world, '''        if (cell->getState() == CellStore::State_Unloaded)
            continue;
        std::vector<ESM::RefId> ids''', '''        if (cell->getState() == CellStore::State_Unloaded)
        {
            if (cell->getCell()->isEsm4() || cell->hasUnloadedLiveReferences())
                mUnindexedFallbackCells.insert(cell);
            else
                mUnindexedFallbackCells.erase(cell);
            continue;
        }
        std::vector<ESM::RefId> ids''')
replace(world, '        mUnindexedLookupCells.erase(cell);',
        '        mUnindexedLookupCells.erase(cell);\n        mUnindexedFallbackCells.erase(cell);')
replace(world, '''                // A metadata-only CSTA sets hasState() even though it cannot
                // introduce a reference; using hasState() here admitted over
                // 25,000 unrelated saved cells in the user's large modlist.
                // Keep unloaded cells that actually own live runtime refs.
                // ESM4 lacks the ESM3 content index and remains conservative.
                // Loaded/preloaded cells awaiting indexing remain searchable.
                for (CellStore* cell : mUnindexedLookupCells)
                    if (cell->getState() != CellStore::State_Unloaded
                        || cell->getCell()->isEsm4() || cell->hasUnloadedLiveReferences())
                        candidates.push_back(cell);''', '''                // Do not walk tens of thousands of metadata-only cells for
                // every script query. Dirty notifications maintain this small
                // exceptional set; live/preloaded IDs are in the index above.
                candidates.insert(candidates.end(), mUnindexedFallbackCells.begin(),
                    mUnindexedFallbackCells.end());''')

# All validations succeeded. Commit the entire in-memory transaction to disk.
for name, text in sources.items():
    if blob(text) != EXPECTED[name]:
        (ROOT / name).write_text(text, encoding='utf-8', newline='\n')
manifest = {'baseline': 'e9476be66251d181d0016a5fd112e1d90e3875f6',
            'files': {n: {'before_blob': EXPECTED[n], 'after_blob': blob(t)} for n, t in sources.items()},
            'edits': edits, 'postload_waiting_stage': 'removed',
            'extra_pre_game_index_preparation': 'removed'}
(ROOT / 'loading-repair-source-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print(f'Applied {len(edits)} lookup-only edits; verified {len(sources)} baseline files. No added loading stage.')
