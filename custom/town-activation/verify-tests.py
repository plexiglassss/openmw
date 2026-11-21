"""Require concrete executed native cases, including isolated feature-off modes."""
import argparse, hashlib, json
from pathlib import Path
from xml.etree import ElementTree as ET

SUITES = {
 'RecentResourceCache': {'RevisitRefreshesLifetimeAndEvictsLeastRecentlyUsed','ByteBudgetOversizeAndEntryBoundAreIndependent','ClearRejectsAnAlreadyRunningProducer','ConcurrentConsumersCannotExceedEitherBound','DestructionCanReenterCacheAfterEvictionAndClear'},
 'ActorAttachmentPlan': {'PreservesCasePrefixFilteringAndDistinctSubgraphs','DeduplicatesMatchingAncestorsInLegacyPointerOrder','KeepsSourceAliveWithoutOwningActors'},
 'ActorRetainedMemory': {'IncludesHiddenRigMorphAndRetainedSourceArrays','ReusedRecipeStillProducesIndependentRigAndMorphInstances'},
 'ActorPreparation': {'ProcessSwitchesRespectRawEnvironmentAndMaster','IdentityIncludesModelSkeletonInjectionOwnerSettingsAndVfs','FactoryCanUsePublicSceneMethodsAndEveryActorIsIndependent','ClearDuringFactoryDiscardsOldGenerationBeforePublication','RejectedGraphsReuseWeakAdmissionEstimatesAcrossFilters','RigRecipeReusesNormalizedFilterButInvalidatesSettingsAndSource','OnlyConsumedTemplatesSurviveOrdinaryCacheExpiryAndClearReleasesThem'},
}
p=argparse.ArgumentParser();p.add_argument('--native',type=Path,required=True);p.add_argument('--actor-off',type=Path,required=True);p.add_argument('--reuse-off',type=Path,required=True);p.add_argument('--master-off',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
checks=[]
for mode,path in [('enabled',a.native),('actor-off',a.actor_off),('reuse-off',a.reuse_off),('master-off',a.master_off)]:
 data=path.read_bytes();root=ET.fromstring(data)
 identities=[(c.get('classname'),c.get('name')) for c in root.iter('testcase')]
 if len(identities)!=len(set(identities)):raise SystemExit('Duplicate testcase identity in '+str(path))
 cases={(c.get('classname'),c.get('name')):c for c in root.iter('testcase')};expected={(s,n) for s,ns in SUITES.items() if mode=='enabled' or s=='ActorPreparation' for n in ns}
 for key in expected:
  if key not in cases:raise SystemExit('Missing test '+'.'.join(key))
  c=cases[key]
  if c.get('status')!='run' or c.get('result')!='completed' or any(c.find(t) is not None for t in ('failure','error','skipped')):raise SystemExit('Unexecuted/failed '+'.'.join(key))
 contract=cases[('ActorPreparation','ProcessSwitchesRespectRawEnvironmentAndMaster')]
 props={p.get('name'):p.get('value') for p in contract.findall('./properties/property')}
 actor,reuse={'enabled':('1','1'),'actor-off':('0','1'),'reuse-off':('1','0'),'master-off':('0','0')}[mode]
 if props.get('actor_expected')!=actor or props.get('reuse_expected')!=reuse:raise SystemExit('Wrong process environment for '+mode)
 checks.append({'mode':mode,'xml':str(path),'sha256':hashlib.sha256(data).hexdigest(),'executed':sorted('.'.join(k) for k in expected)})
a.output.write_text(json.dumps({'result':'PASS','checks':checks,'installed_game_tested':False},indent=2)+'\n');print('Verified',sum(len(c['executed']) for c in checks),'town activation native executions')
