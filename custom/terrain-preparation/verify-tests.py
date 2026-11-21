"""Reject missing, skipped or failed terrain regressions in executed native XML."""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser()
p.add_argument('--native',type=Path)
p.add_argument('--graphics',type=Path)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if not a.native and not a.graphics:p.error('An executed native or graphics XML report is required')
required_native={
    'ConcurrentIdenticalRequestsFillOnce','IndependentChunksOverlapAndJoin','SerialAndNestedPoolFallbacksComplete',
    'CancellationSkipsUnstartedWorkButJoinsLiveFills','FailureDoesNotPublishPartialDataOrPoisonRetry',
    'ClearWaitsForProducerAndDiscardsOldData','IdentityIncludesSizeLodAndWorldOwner',
    'UnusedVerticesExpireAndHeldVerticesSurvive','SharedArraysRetainDistinctStitchTopologyAndRayHits',
}
checks=[]
for path,suite,names in [(a.native,'TerrainVertexPreparation',required_native),
                         (a.graphics,'TerrainVertexSharingGL',{'StitchVariantsDrawAfterSiblingReleaseAndBufferRecompile'})]:
    if path is None:continue
    data=path.read_bytes();tree=ET.fromstring(data)
    found={c.attrib['name']:c for c in tree.iter('testcase') if c.attrib.get('classname')==suite}
    for name in sorted(names):
        if name not in found:raise SystemExit('Missing executed regression: '+suite+'.'+name)
        case=found[name]
        if case.attrib.get('status')!='run' or case.attrib.get('result')!='completed' or any(case.find(t) is not None for t in ['failure','error','skipped']):
            raise SystemExit('Failed or unexecuted regression: '+suite+'.'+name)
    checks.append({'xml':str(path),'sha256':hashlib.sha256(data).hexdigest(),'suite':suite,'executed':sorted(names)})
a.output.write_text(json.dumps({'result':'PASS','checks':checks,'installed_game_tested':False},indent=2)+'\n')
print('Verified',sum(len(c['executed']) for c in checks),'executed terrain regressions')
