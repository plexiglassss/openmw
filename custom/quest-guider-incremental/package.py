"""Package the reviewed override only after test evidence matches its exact Lua bytes."""
import argparse,hashlib,json,shutil,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--evidence',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
e=json.loads(a.evidence.read_text());assert e['result']=='PASS' and {x['runtime'] for x in e['results']}=={'lua51','lua54','luajit21'}
assert len(e['results'])==3 and all(row['passed']==len(row['checks']) and row['passed']>=9 for row in e['results'])
for name,digest in e['sha256'].items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,name
folder=HERE/'Quest Guider Lite - Incremental Nearby Queries';prov=json.loads((HERE/'provenance.json').read_text())
for name,digest in prov['retained_helpers'].items():assert hashlib.sha256((folder/'scripts/quest_guider_lite'/name).read_bytes()).hexdigest()==digest
files=sorted(f for f in folder.rglob('*') if f.is_file());expected={'README.md','scripts/quest_guider_lite/main.lua','scripts/quest_guider_lite/storage/nearbyQueries.lua','scripts/quest_guider_lite/parallelPositionRanking.lua','scripts/quest_guider_lite/rankQuestPositions.lua'}
assert {f.relative_to(folder).as_posix() for f in files}==expected
with zipfile.ZipFile(a.output,'w',zipfile.ZIP_DEFLATED) as z:
 for f in files:z.write(f,f.relative_to(HERE))
 z.write(a.evidence,'verification/quest-nearby-tests.json');z.write(HERE/'provenance.json','verification/provenance.json')
with zipfile.ZipFile(a.output) as z:
 assert z.testzip() is None
 for f in files:assert z.read(f.relative_to(HERE).as_posix())==f.read_bytes()
print('Verified override package:',a.output)
