"""Check the public Lua binding and the independent-value control in the same executable."""
import json
from pathlib import Path
from xml.etree import ElementTree

cases = {
    'OrdinarySpellReadsPreserveValuesAndIndependentIterators',
    'SpellReadDetailsValidateMutableEffectsAndLegacySaveReferences',
    'OrdinarySpellReadsBenchmark',
}

def read_results(path):
    found = {}
    for case in ElementTree.parse(path).iter('testcase'):
        if case.get('classname') != 'MWLuaMagicBindingsTest' or case.get('name') not in cases:
            continue
        assert case.get('result') == 'completed' and case.get('status') == 'run'
        assert all(case.find(tag) is None for tag in ('failure', 'skipped', 'error'))
        found[case.get('name')] = {row.get('name'): int(row.get('value'))
                                    for row in case.findall('./properties/property')}
    assert set(found) == cases
    return found['OrdinarySpellReadsBenchmark']

modes = {name: read_results(path) for name, path in [('shared', 'openmw-tests.xml'), ('copied', 'previous-spell-reads.xml')]}
shared, copied = modes.values()
for key in ('actors', 'entries', 'effects_exported', 'checksum'):
    assert shared[key] == copied[key] > 0
for mode in modes.values():
    assert mode['median_ns_per_four_scans'] > 0
    assert mode['copies'] + mode['reuses'] == mode['entries']
assert shared['copies'] == 48 and copied['copies'] == copied['entries'] and copied['reuses'] == 0
assert shared['effects_copied'] == 3 * shared['copies'] < copied['effects_copied']
report = {'result': 'PASS', 'cases': sorted(cases), 'modes': modes,
          'scope': 'Four public Lua traversals over the same 32 actors per group. Eight empty actors. Independent copies and shared immutable values use the same iterator. Town frame time requires a player capture.'}
Path('active-spell-reads.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(report)
