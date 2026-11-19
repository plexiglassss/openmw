"""Require executed production geometry tests and their actual work counters."""
from pathlib import Path
import hashlib
import json
import sys
import xml.etree.ElementTree as ET


NAMES = {
    'ConcurrentRequestsShareActualTriangleTraversal',
    'UnrelatedTileChangeDoesNotDiscardOrRetryBuild',
    'ChangedTileCannotBeOverwrittenByEarlierBuild',
    'RangeExitAndReentryCannotRestoreObsoleteBuild',
    'WorldChangeClearsFiniteAndInfiniteInputs',
    'FailureReachesFollowersAndAllowsRetry',
    'SnapshotRetainsShapeAfterWorldAndOwnerRemoval',
    'UncachedToolPathIgnoresRangeAndMatchesCachedGeometry',
    'SnapshotAllocationDependsOnLocalMatches',
    'EarlierCompletionDoesNotRemoveNewPendingBuild',
    'WorldExitAndReentryDoesNotRestoreEarlierGeneration',
    'DifferentTilesCanTraverseConcurrently',
}


def verify(path, optimized):
    data = Path(path).read_bytes()
    root = ET.fromstring(data)
    all_cases = list(root.iter('testcase'))
    if len(all_cases) != int(root.get('tests', 0)):
        raise ValueError('Test count mismatch: ' + str(path))
    for summary in [root, *root.iter('testsuite')]:
        if any(int(summary.get(key, 0)) != 0 for key in ('failures', 'errors', 'disabled')):
            raise ValueError('Unsuccessful test summary: ' + str(summary.attrib))
    identities = [(c.get('classname'), c.get('name')) for c in all_cases]
    if len(set(identities)) != len(identities):
        raise ValueError('Duplicate testcase identity')
    for case in all_cases:
        if (case.get('status') != 'run' or case.get('result') != 'completed'
                or any(case.find(tag) is not None for tag in ('failure', 'error', 'skipped'))):
            raise ValueError('Unsuccessful test: ' + str(case.attrib))
        properties = [p.get('name') for p in case.findall('./properties/property')]
        if len(set(properties)) != len(properties):
            raise ValueError('Duplicate test property: ' + str(case.attrib))
    selected = [c for c in all_cases if c.get('classname') == 'NavMeshGeometryPreparation']
    if len(selected) != len(NAMES) or {c.get('name') for c in selected} != NAMES:
        raise ValueError('Missing or duplicate navigation geometry tests')
    if sum(c.get('classname') == 'DetourNavigatorTileCachedRecastMeshManagerTest' for c in all_cases) != 38:
        raise ValueError('Existing geometry regression coverage changed')
    counters = {
        c.get('name'): {p.get('name'): int(p.get('value')) for p in c.findall('./properties/property')}
        for c in selected if c.find('properties') is not None
    }
    expected = {
        'ConcurrentRequestsShareActualTriangleTraversal': {'triangle_traversals': 1 if optimized else 2},
        'SnapshotAllocationDependsOnLocalMatches': {
            'active_objects': 1025, 'snapshot_objects': 1, 'snapshot_capacity': 1 if optimized else 1025},
    }
    if counters != expected:
        raise ValueError('Wrong actual traversal/allocation counters: ' + repr(counters))
    return {'sha256': hashlib.sha256(data).hexdigest(), 'new_tests': len(selected),
            'existing_tests': 38, 'work_counters': counters}


def main():
    if len(sys.argv) != 4:
        raise SystemExit('Usage: verify-navigation-geometry.py optimized.xml previous.xml master-off.xml')
    results = {mode: verify(path, mode == 'optimized') for mode, path in zip(
        ('optimized', 'previous', 'master_off'), sys.argv[1:])}
    Path('navigation-geometry-tests.json').write_text(json.dumps({
        'result': 'PASS', 'files': results, 'in_game_performance_measured': False,
    }, indent=2) + '\n')
    print('PASS: navigation geometry, lifetime and real work counters in all three modes.')


if __name__ == '__main__':
    main()
