import csv
import sys
from pathlib import Path
rows = list(csv.DictReader(Path(sys.argv[1]).open(newline='', encoding='utf-8')))
assert any(r['category'] == 'lua-load' and r['source'] == 'test1.lua' for r in rows)
assert any(r['category'] == 'lua' and r['source'] == 'test1.lua' and r['callback'] == 'onUpdate' for r in rows)
assert any(r['category'] == 'lua' and r['callback'] == 'Event1' for r in rows)
assert all(float(r['self_us']) <= float(r['total_us']) for r in rows)
print('PASS: real Lua script loading, engine callbacks and event callbacks exported')
