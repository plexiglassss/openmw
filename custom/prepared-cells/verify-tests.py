"""Require executed native preparation tests in both independently recorded modes."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET

REQUIRED = {
    'DormantAabbAndDebugDrawingStayHiddenUntilPublication',
    'ActorAndProjectileSweepsSeeOnlyPublishedCollision',
    'StagingYieldsToBusyPhysicsQueryWithoutWaiting',
    'PreparationCreditOutlivesRetainedCollisionAndGraphDestructors',
    'DormantBroadphaseIsInvisibleAndPublicationUpdatesImmediateQueries',
    'StagingHonoursReadinessDeadlineAndObjectLimit',
    'CancelledStagedObjectsRetireWithoutBecomingQueryable',
    'PhysicsTeardownDetachesExternallyRetainedDormantObjects',
    'StaleStagedReferenceFallsBackAndNeverExposesOldCollision',
    'ManagerRequiresCompleteStagingAndClearRemovesDormantHandles',
    'ManagerInvalidationCancelsStagedPacketsAndQueueReplacementUnstages',
    'WorkerPreparationLeavesLiveRenderingPhysicsAndReferenceUntouched',
    'OrderedPublicationBindsIdentityAndCollisionExactlyOnce',
    'SameAssetProducesIndependentLiveObjects',
    'PreparedAndOrdinaryPublicationHaveEquivalentTransformsAndHits',
    'ChangedPositionScaleEnableCountAndModelRejectStaleInstances',
    'UnpublishedDiscardAndCancelledPreparationNeverCreateLiveObjects',
    'PagedRenderingConsumesOnlyCollisionAndNeverPublishesIndividualNode',
    'PostponedPhysicsDoesNotLeakPreparedCollisionIntoWorld',
    'DuplicatePublicationRejectionPreservesTheExistingObject',
    'ManagerHonoursOptInAndRequiresGraphicsAcknowledgement',
    'PagedManagerPreparesCollisionWithoutGraphicsOrDuplicateRenderGraphs',
    'ManagerRejectsVfsModelAndPagingChanges',
    'InstancePreloadingSettingSelectsCollisionOnlyAndInvalidatesOldMode',
    'ManagerRejectsReleasedGraphicsAndOldSaveGeneration',
    'DedicatedPreparationQueueDoesNotOccupyTheRequiredArrivalQueue',
    'DisabledPreloadingAdmitsNoSpeculation',
    'ManagerCancelsOldQueueAndPinsContextsDuringQueuedWork',
    'CacheMaintenancePreservesExplicitCompileCompletion',
}
COMPONENTS = {
    'SpeculativeRetirementNeverWaitsForAFullCleanupQueue',
    'ExternalStagedOwnershipRetainsChargeAfterJobRetirement',
    'ChargeCallbackFailureCompletesAsFailureAndCancelsReceipt',
    'AdmissionPreflightRejectsSaturatedQueueBeforeSnapshotConstruction',
    'CancelledAndRetiringGraphsRemainChargedUntilWorkerRelease',
    'CancelledCpuWorkStillConsumesSeparateCpuCapacity',
    'CpuCompletionWaitsForTheExplicitGraphicsGate',
    'CancelledCompletionCannotResurrectTheOldGeneration',
    'BudgetStopsFurtherAdmissionUntilOldResultsRetire',
    'DestructionCancelsAQueueThatHasNoWorkers',
}
GRAPHICS = 'ActualGraphicsCompilationPublishesVisibleGeometryOnlyAfterCompletion'

def check(case, name):
    if case is None:
        raise ValueError('Missing executed test: ' + name)
    if case.get('status') != 'run' or case.get('result') != 'completed' or any(
            case.find(tag) is not None for tag in ('failure', 'error', 'skipped')):
        raise ValueError('Failed, skipped or incomplete test: ' + name)
    properties = case.findall('./properties/property')
    if len({p.get('name') for p in properties}) != len(properties):
        raise ValueError('Duplicate test property: ' + name)
    return {p.get('name'): p.get('value') for p in properties}

def read(path):
    data = path.read_bytes()
    root = ET.fromstring(data)
    identities = [(c.get('classname'), c.get('name')) for c in root.iter('testcase')]
    if len(set(identities)) != len(identities):
        raise ValueError('Duplicate testcase identity: ' + str(path))
    for suite in [root, *root.findall('testsuite')]:
        cases = list(suite.iter('testcase'))
        for attribute, tag in (('failures', 'failure'), ('errors', 'error'), ('skipped', 'skipped')):
            observed = sum(c.find(tag) is not None for c in cases)
            if attribute in suite.attrib and int(suite.get(attribute)) != observed:
                raise ValueError('Inconsistent suite ' + attribute + ': ' + str(path))
    return root, {'path': str(path), 'sha256': hashlib.sha256(data).hexdigest()}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--off', type=Path, required=True)
    parser.add_argument('--on', type=Path, required=True)
    parser.add_argument('--components', type=Path, required=True)
    parser.add_argument('--require-graphics', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    checks, benchmarks = [], []
    for path, expected in ((args.off, 'false'), (args.on, 'true')):
        root, receipt = read(path)
        cases = {(c.get('classname'), c.get('name')): c for c in root.iter('testcase')}
        for (suite, name), case in cases.items():
            if suite in ('PreparedCellTest', 'RenderModes/PreparedCellBenchmark'):
                check(case, name)
        names = REQUIRED | ({GRAPHICS} if args.require_graphics else set())
        for name in sorted(names):
            props = check(cases.get(('PreparedCellTest', name)), name)
            if props.get('prepared_cells_enabled') != expected:
                raise ValueError('Wrong/missing executed mode: ' + name)
        for index in range(4):
            name = 'DensePublicationMeasuresPreparedAndOrdinaryOwnerCost/' + str(index)
            props = check(cases.get(('RenderModes/PreparedCellBenchmark', name)), name)
            if props.get('prepared_cells_enabled') != expected:
                raise ValueError('Wrong benchmark mode: ' + name)
            if props.get('prepared_static_count') != '1024' or props.get('rounds') != '12':
                raise ValueError('Unexpected benchmark workload: ' + name)
            for key in ('prepared_owner_samples_ms', 'ordinary_owner_samples_ms', 'background_samples_ms',
                        'staged_owner_samples_ms', 'staging_total_samples_ms', 'staging_slice_samples_ms', 'empty_query_samples_ms', 'dormant_query_samples_ms'):
                samples = [float(v) for v in props.get(key, '').split(',')]
                count_ok = len(samples) >= 12 if key == 'staging_slice_samples_ms' else len(samples) == (24 if key == 'background_samples_ms' else 12)
                if not count_ok or any(not math.isfinite(v) or v < 0 for v in samples):
                    raise ValueError('Invalid benchmark samples: ' + key)
            benchmarks.append({'mode': expected, 'case': name, **props})
        checks.append({**receipt, 'mode': expected, 'required_executed': len(names) + 4})
    root, receipt = read(args.components)
    cases = {c.get('name'): c for c in root.iter('testcase') if c.get('classname') == 'AsyncPreparation'}
    for name, case in cases.items():
        check(case, name)
    for name in sorted(COMPONENTS):
        check(cases.get(name), name)
    checks.append({**receipt, 'suite': 'AsyncPreparation', 'required_executed': len(COMPONENTS)})
    args.output.write_text(json.dumps({
        'result': 'PASS', 'checks': checks, 'benchmarks': benchmarks,
        'actual_graphics_required': args.require_graphics,
        'whole_cell_activation_tested': False, 'installed_cities_tested': False,
        'seamless_performance_established': False,
    }, indent=2) + '\n')
    print('Verified', sum(c['required_executed'] for c in checks), 'required executed cases; no seamless-performance claim.')

if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, ET.ParseError) as error:
        raise SystemExit(str(error))
