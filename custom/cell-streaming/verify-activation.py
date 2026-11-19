"""Require actual cell-activation tests, rollback parity and operation counters."""
from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET

ENGINE = {
    'RegisteredCopySurvivesTemporaryAndIsConsumedOnce',
    'IdempotentRegistrationStillObservesFlagChange',
    'DisabledObjectClearsFlagWithoutAddingCollision',
    'InactivePlacementAndMovementUseCurrentCell',
    'ActiveToInactiveAndBackRemainsPending',
    'ActiveCellMoveDoesNotRetainOldLocation',
    'DeregisteredReferencesAreNotVisited',
    'OldReferenceRemovalDoesNotEraseReplacement',
    'DynamicCountZeroMatchesCellAccessibility',
    'ContentCountZeroClearsFlagWithoutCollision',
    'ContentDeletionIsSkippedUntilRestored',
    'ExceptionDoesNotLoseUnprocessedObjects',
    'CallbackMayClearRegistryWithoutInvalidatingIteration',
    'ResetDoesNotCarryPendingWorkIntoNewWorld',
    'DrainCostDependsOnPendingObjectsNotWholeGrid',
}
QUEUE_ONLY = {'DeregisteredReferencesAreNotVisited', 'OldReferenceRemovalDoesNotEraseReplacement',
              'ExceptionDoesNotLoseUnprocessedObjects', 'CallbackMayClearRegistryWithoutInvalidatingIteration'}
CACHE = {'ExactScaleAndModelSelectExclusiveInstances', 'DuplicateOccurrencesDoNotShareMutableShapes',
         'NonfiniteKeysNeverEnterOrderedCache', 'RetentionFollowsPreloaderOwnershipAndClearInvalidates',
         'ParallelConsumersCanTakeEachInstanceOnlyOnce', 'PreparedCompoundAndAvoidanceBoundsMatchDemandScaling',
         'MatchingScaleSkipsWorkButRepairsAvoidanceScaleIndependently', 'MatchingCompoundActivationBenchmark'}
PREPARATION = {'ActualManagerPublishesScaledInstanceBeforeActivation', 'ChangedRuntimeScaleUsesSafeFallback',
               'OrdinaryUnscaledPreloadCanStillSupplyScaledActivation', 'ResourceWithoutCollisionShapeCanBePreloaded'}


def read(path):
    data = Path(path).read_bytes()
    root = ET.fromstring(data)
    cases = list(root.iter('testcase'))
    if len(cases) != int(root.get('tests', 0)):
        raise ValueError('Test count mismatch: ' + str(path))
    for summary in [root, *root.iter('testsuite')]:
        if any(int(summary.get(k, 0)) for k in ('failures', 'errors', 'disabled')):
            raise ValueError('Unsuccessful test summary: ' + str(path))
    indexed = {}
    for case in cases:
        identity = (case.get('classname'), case.get('name'))
        if identity in indexed or case.get('status') != 'run' or case.get('result') != 'completed':
            raise ValueError('Duplicate or unexecuted test: ' + str(identity))
        if any(case.find(t) is not None for t in ('failure', 'error', 'skipped')):
            raise ValueError('Unsuccessful test: ' + str(identity))
        properties = case.findall('./properties/property')
        if len({p.get('name') for p in properties}) != len(properties):
            raise ValueError('Duplicate property: ' + str(identity))
        indexed[identity] = {p.get('name'): int(p.get('value')) for p in properties
                             if p.get('name') in {'pending_enumerated', 'sparse_enabled', 'inserted', 'matching_scale_calls'}}
    return indexed, hashlib.sha256(data).hexdigest()


def verify(components_path, engine_path, optimized):
    components, component_hash = read(components_path)
    engine, engine_hash = read(engine_path)
    for suite, names, cases in [('CollisionInstanceCacheTest', CACHE, components),
                                ('CollisionPreparationTest', PREPARATION, components),
                                ('PostponedPhysicsTest', ENGINE if optimized else ENGINE-QUEUE_ONLY, engine)]:
        actual = {name for (group, name) in cases if group == suite}
        if actual != names:
            raise ValueError('Missing or unexpected '+suite+' tests: '+repr(actual ^ names))
    queue = engine[('PostponedPhysicsTest', 'DrainCostDependsOnPendingObjectsNotWholeGrid')]
    expected = {'pending_enumerated': 3 if optimized else 0, 'sparse_enabled': int(optimized), 'inserted': 3}
    if queue != expected:
        raise ValueError('Wrong pending-work counters: '+repr(queue))
    scale = components[('CollisionInstanceCacheTest', 'MatchingScaleSkipsWorkButRepairsAvoidanceScaleIndependently')]
    if scale != {'matching_scale_calls': 1 if optimized else 2}:
        raise ValueError('Wrong scaling-work counter: '+repr(scale))
    return {'components_sha256': component_hash, 'engine_sha256': engine_hash,
            'component_tests': len(CACHE)+len(PREPARATION),
            'engine_tests': len(ENGINE if optimized else ENGINE-QUEUE_ONLY),
            'queue_counters': queue, 'scale_counters': scale}


def main():
    results = {'optimized': verify('components-tests.xml', 'openmw-tests.xml', True)}
    for mode in ('previous', 'master-off'):
        results[mode] = verify('activation-'+mode+'-components.xml', 'activation-'+mode+'-engine.xml', False)
    Path('activation-tests.json').write_text(json.dumps({
        'result': 'PASS', 'modes': results, 'in_game_performance_measured': False,
    }, indent=2)+'\n')
    print('PASS: cell activation lifecycle, scaled collision preparation and actual work counters in all modes.')


if __name__ == '__main__':
    main()
