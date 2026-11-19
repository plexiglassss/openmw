"""Check that the shipped Windows binaries really ran the enabled and disabled controls."""
from pathlib import Path
import xml.etree.ElementTree as ET


def cases(path):
    suite = ET.parse(path).getroot()
    assert int(suite.get('failures')) == 0 and int(suite.get('errors')) == 0, path
    return {t.get('classname')+'.'+t.get('name'): t for t in suite.iter('testcase')
            if t.get('status') == 'run' and t.get('result') == 'completed' and not any(t.find(tag) is not None for tag in ('error','failure','skipped'))}


for mode, name in [('1','openmw-tests.xml'), ('0','previous-parallel-queries.xml')]:
    t = cases(Path(name))['ParallelQueryBindings.PublicWorkerAndDistanceSnapshotRoundTrip']
    props = {p.get('name'):p.get('value') for p in t.findall('properties/property')}
    assert all(props[key] == mode for key in ('batched_visual_rays','async_distances','isolated_workers')), props
comp = cases('components-tests.xml')
assert 'ParallelWorker.IndependentThreadsPublishInOrderAndBoundOutstandingResults' in comp
assert 'ParallelWorker.LimitFailureDoesNotContaminateTheNextFreshLuaState' in comp
assert any(name.startswith('VisualRayBatch.') for name in comp)
print('PASS: Windows parallel query bindings, cumulative control, two worker threads and visual hit parity')
