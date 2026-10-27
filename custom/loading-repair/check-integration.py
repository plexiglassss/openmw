from pathlib import Path
root = Path(__file__).resolve().parents[2]

def get(name):
    return (root / name).read_text(encoding='utf-8')

def check(condition, message):
    if not condition:
        raise SystemExit(message)

engine = get('apps/openmw/engine.cpp')
start = engine.index('bool OMW::Engine::frame(')
end = engine.index('OMW::Engine::Engine(', start)
frame = engine[start:end]
check(frame.index('mLuaWorker->finishGc()') < frame.index('handoff.releaseIfPending()')
      < frame.index('mInputManager->update('), 'Cover release must follow GC and precede input')
check(frame.index('mViewer->renderingTraversals()') < frame.index('mLuaWorker->finishUpdate(')
      < frame.index('handoff.observe('), 'Cannot sample readiness before render and Lua complete')
check('mInputManager->update(frametime, handoff.active(), handoff.active())' in frame,
      'Gameplay input must be suppressed during covered initialization')
check(frame.count('frametime = 0.f;') == 2, 'Must suppress simulation before and after a mid-frame save load')
check('if (!handoff.active())\n            mLuaWorker->gc();' in frame,
      'Must not launch an unmeasured GC job before handoff')
check('handoffCompleted = false;' in frame, 'Frame errors must not be reported as settled')
world = get('apps/openmw/mwworld/worldmodel.cpp')
selection = world.split('if (useIndexedInactiveCellCandidates())', 1)[1].split('            else', 1)[0]
check('for (CellStore* cell : mUnindexedLookupCells)' not in selection,
      'Regression: indexed query still scans every unloaded cell')
check('mUnindexedFallbackCells.begin()' in selection, 'Exceptional candidates not included')
check('mUnindexedFallbackCells.clear();' in world, 'Clear must discard fallback membership')
check('mUnindexedFallbackCells.erase(cell);' in world, 'Loaded cells must leave fallback set')
state = get('apps/openmw/mwstate/statemanagerimp.cpp')
check(state.index('->prepareCellLookup();') < state.index('Loading::gameplayHandoff().request();'),
      'Prepare the index before handoff starts')
check('void MWState::StateManager::cleanup(bool force)\n{\n    Loading::gameplayHandoff().cancel();' in state,
      'Cleanup must cancel a previous cover')
screen = get('apps/openmw/mwgui/loadingscreen.cpp')
check('visible = visible || (mHandoffCover && Loading::gameplayHandoff().active());' in screen,
      'Removing loading GUI mode must not hide its visual cover')
check('Loading::gameplayHandoff().detachCover(this);' in screen, 'Screen lifetime not guarded')
check('if (mNestedLoadingCount == 0)\n                    setVisible(false);' in screen,
      'Handoff release must not hide a genuine nested loading screen')
print('Loading handoff integration/order checks passed. These are source checks, not an in-game test.')
