import csv
import sys
from pathlib import Path
rows = list(csv.DictReader(Path(sys.argv[1]).open(newline='', encoding='utf-8')))
def get(source):
    found = [r for r in rows if r['source'] == source]
    assert len(found) == 1, (source, found)
    return found[0]
parent = get('parent.lua')
child = get('child,"quoted".lua')
assert float(parent['total_us']) >= float(child['total_us']) > 0
children = sum(float(get(s)['total_us']) for s in ['child,"quoted".lua','repeated.lua','exception.lua'])
assert abs(float(parent['total_us']) - float(parent['self_us']) - children) < .01
assert int(get('repeated.lua')['calls']) == 3
assert get('last')['frame'] == '43'
assert get('Lua')['thread'] != parent['thread']
assert all(float(r['self_us']) >= 0 and float(r['total_us']) >= float(r['self_us']) for r in rows)
assert all(float(r['last_us']) >= float(r['first_us']) for r in rows)
print('PASS: nesting, exclusive accounting, aggregation, exception unwinding, CSV escaping, threads, frame IDs, shutdown flush')

batched = [r for r in rows if r['source'] == 'batched']
assert len(batched) == 128
assert {int(r['frame']) for r in batched} == set(range(44, 172))
assert all(int(r['calls']) == 1 for r in batched)
print('PASS: buffered frames retained separately through final thread flush')
