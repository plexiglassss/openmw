"""Require actual execution of the staged-arrival correctness tests, in both modes."""
import argparse
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('prepared_verifier', Path(__file__).with_name('verify-tests.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
ENGINE = ('RequiredMilestoneDoesNotWaitForDistantWorkerCompletion',
          'TerrainHasCapacityWhenEverySpeculativeWorkerIsOccupied',
          'UnpresentedPredictionDoesNotOccupyTheModelQueue')
COMPONENTS = ('GroundNeverCertifiesAnExactObjectQuery',
              'CamerasShareAdmissionQuotaAndRequiredNodesBypassIt',
              'BackpressureUnblocksOnAdmissionOrCancellation',
              'FailedContinuationDisablesStagedCoverage',
              'ExistingCameraCoverageStillWaitsForPendingGraphics',
              'InvalidationReleasesBackpressureAndCancellationRemovesOwnedCompileSets')

def main():
    parser = argparse.ArgumentParser()
    for option in ('off', 'on', 'components', 'output'):
        parser.add_argument('--' + option, type=Path, required=True)
    args = parser.parse_args()
    checks = []
    for path, mode in ((args.off, 'false'), (args.on, 'true')):
        root, receipt = base.read(path)
        cases = {(c.get('classname'), c.get('name')): c for c in root.iter('testcase')}
        for name in ENGINE:
            props = base.check(cases.get(('StagedArrival', name)), name)
            if props.get('staged_arrival_enabled') != mode:
                raise ValueError('Wrong or missing staged mode: ' + name)
        if mode == 'true':
            for suite, name in (
                ('StagedTerrain', 'FirstCullAndNearRayReusePublishedCoverageWhileFarWorkerIsBlocked'),
                ('ArrivalCancellation', 'RequiredArrivalDoesNotJoinAnObsoleteRunningPrediction')):
                base.check(cases.get((suite, name)), name)
        checks.append({**receipt, 'mode': mode})
    root, receipt = base.read(args.components)
    cases = {c.get('name'): c for c in root.iter('testcase') if c.get('classname') == 'StagedArrival'}
    for name in COMPONENTS:
        base.check(cases.get(name), name)
    checks.append({**receipt, 'suite': 'StagedArrival'})
    args.output.write_text(json.dumps({'result': 'PASS', 'checks': checks,
        'city_performance_established': False, 'all_visual_queries_nonblocking': False}, indent=2) + '\n')
    print('Verified staged arrival, exact-query fallback, cancellation and reserved terrain capacity.')

if __name__ == '__main__':
    main()
