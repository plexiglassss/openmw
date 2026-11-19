"""Verify real batch tests and legacy/master rollback operation counts."""
from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET

EXPECTED = {'components': [['CellObjectIndexTest', 'MovementReplacementAndRemovalKeepOneOwner'], ['CellObjectIndexTest', 'UnloadingOneCellDoesNotEnumerateOtherCells'], ['PreloadPlanCacheTest', 'ReusesTopologyAndRebuildsOnEitherRevision'], ['PreloadPlanCacheTest', 'CapacityEvictsLeastUsedAndResetDropsPointerPlans'], ['PreloadPlanCacheTest', 'FailedRebuildDoesNotValidateStalePlanAndCanRetry'], ['PendingPreloadTest', 'ReusesOnlyUnfinishedMatchingGenerationAndAllowsFailureRetry'], ['PendingPreloadTest', 'CompletedKeysArePrunedAndReplacedJobsKeepOwnerReferences'], ['PreloadRetentionTest', 'ProtectsRecentExpensiveWorkButCannotPinObsoleteCells'], ['PreloadRetentionTest', 'StrictGraceAndRefreshWinOverCost'], ['PreloadRetentionTest', 'InvalidCostsCannotPoisonRanking'], ['SlicedCleanupTest', 'MaintenanceYieldsToArrivalsAndCannotStarve'], ['SlicedCleanupTest', 'CancelledMaintenanceNeverRunsAndSignalsWaiters'], ['SlicedCleanupTest', 'StopRejectsRunningContinuationAndCompletesDroppedWork'], ['SlicedCleanupTest', 'WorkerPublishesCompletionOnlyAfterFinalSlice'], ['SlicedCleanupTest', 'DetachedObjectsReleaseOnceInBoundedSlices'], ['SlicedCleanupTest', 'LargeBacklogIsBoundedAndEventuallyReleased'], ['ResourceResourceSystem', 'cleanupRestartsSafelyWhenManagersChangeBetweenSlices'], ['ResourceResourceSystem', 'managerRemovalWaitsForRunningSliceBeforeDestruction'], ['ResourceResourceSystem', 'cleanupStepsVisitEachManagerOnceAndKeepTimestamp']], 'engine': [['RendererCellIndexTest', 'ActiveMoveSurvivesUnloadingOldCell'], ['RendererCellIndexTest', 'RemovedObjectAndReinsertedObjectHaveNoStaleMembership'], ['RendererCellIndexTest', 'SmallCellUnloadVisitsOnlyItsAnimations'], ['DoorPreloadPlanTest', 'InsertionInvalidatesEvenWhenPreviousSnapshotWasEmpty'], ['DoorPreloadPlanTest', 'StateReplacementAndPositionRemainVisible'], ['DoorPreloadPlanTest', 'MovingReferencesAdvancesBothMembershipRevisions']]}

def read(path):
    data = Path(path).read_bytes()
    root = ET.fromstring(data)
    cases = list(root.iter('testcase'))
    if len(cases) != int(root.get('tests', 0)):
        raise ValueError('Test count mismatch: ' + str(path))
    for node in [root, *root.iter('testsuite')]:
        if any(int(node.get(k, 0)) for k in ('failures', 'errors', 'disabled')):
            raise ValueError('Failed summary: ' + str(path))
    indexed = {}
    for case in cases:
        key = (case.get('classname'), case.get('name'))
        if key in indexed or case.get('status') != 'run' or case.get('result') != 'completed':
            raise ValueError('Duplicate or unexecuted case: ' + str(key))
        if any(case.find(t) is not None for t in ('failure', 'error', 'skipped')):
            raise ValueError('Failed/skipped case: ' + str(key))
        props = case.findall('./properties/property')
        if len({p.get('name') for p in props}) != len(props):
            raise ValueError('Duplicate property: ' + str(key))
        indexed[key] = {p.get('name'): p.get('value') for p in props}
    return indexed, hashlib.sha256(data).hexdigest()


def verify(component_path, engine_path, optimized, exact=False):
    c, ch = read(component_path)
    e, eh = read(engine_path)
    for kind, actual in [('components', c), ('engine', e)]:
        wanted = {tuple(x) for x in EXPECTED[kind]}
        if not wanted <= actual.keys() or (exact and wanted != actual.keys()):
            raise ValueError('Missing/unexpected ' + kind + ' batch tests')
    counter = e[('RendererCellIndexTest', 'SmallCellUnloadVisitsOnlyItsAnimations')]
    if int(counter.get('visited', -1)) != (3 if optimized else 1003) or int(counter.get('removed', -1)) != 3:
        raise ValueError('Renderer production-path operation counts differ')
    return {'component_sha256': ch, 'engine_sha256': eh, 'component_cases': len(c),
            'engine_cases': len(e), 'renderer_visited': int(counter['visited']), 'renderer_removed': 3}


def main():
    modes = {
        'optimized': verify('components-tests.xml', 'openmw-tests.xml', True),
        'previous_batch': verify('batch-previous-components.xml', 'batch-previous-engine.xml', False, True),
        'master_off': verify('batch-master-off-components.xml', 'batch-master-off-engine.xml', False, True),
    }
    output = {'result': 'PASS', 'modes': modes, 'expected_cases': EXPECTED,
              'in_game_performance_measured': False}
    Path('streaming-batch-tests.json').write_text(json.dumps(output, indent=2) + '\n')
    print('PASS: native cell/door lifecycle, bounded cache/cleanup, shutdown and rollback operation counts.')


if __name__ == '__main__':
    main()
