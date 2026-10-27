"""Author-side, exact-baseline source edits. Never run on a user's installation.
CI applies these edits before compilation and archives the complete resulting diff.
All source blobs and replacement counts are checked before any file is written.
"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
EXPECTED = {
    'apps/openmw/engine.cpp': '569b7ea2871e6e756d3ce16a80b9e6cdf8b65964',
    'apps/openmw/mwgui/loadingscreen.cpp': '00f2a04d44b1df4456a316e44dd8959b048114b3',
    'apps/openmw/mwgui/loadingscreen.hpp': '268e676b011b99eccee41751f4df93e78e7dae9b',
    'apps/openmw/mwstate/statemanagerimp.cpp': '9c8621e46bce58a84e2c8501d93fad6e02e6bcf4',
    'apps/openmw/mwworld/worldmodel.cpp': 'd0e9d1c61392dbfa4983dcb28bdb65d35d3dfe35',
    'apps/openmw/mwworld/worldmodel.hpp': '233ca8ff1665323ca6efd96454c4bd3cd8ca88fd',
}

def blob(text):
    data = text.encode('utf-8')
    return hashlib.sha1(b'blob ' + str(len(data)).encode('ascii') + b'\0' + data).hexdigest()

sources = {}
for name, sha in EXPECTED.items():
    text = (ROOT / name).read_text(encoding='utf-8')
    if blob(text) != sha:
        raise SystemExit(f'Refusing unknown baseline for {name}: {blob(text)} != {sha}')
    sources[name] = text

edits = []
def replace(name, old, new):
    text = sources[name]
    if text.count(old) != 1:
        raise SystemExit(f'Expected exactly one anchor in {name}: {old[:100]!r}; got {text.count(old)}')
    sources[name] = text.replace(old, new, 1)
    edits.append({'file': name, 'anchor': old[:90]})

engine = 'apps/openmw/engine.cpp'
replace(engine, '#include "engine.hpp"', '#include "engine.hpp"\n#include <components/loadinglistener/gameplayhandoff.hpp>')
replace(engine, '''    PostLoadFrameReport postLoadFrame;
    using Diagnostics::PostLoad::Phase;''', '''    PostLoadFrameReport postLoadFrame;
    auto& handoff = Loading::gameplayHandoff();
    const auto handoffGeneration = handoff.generation();
    const auto handoffFrameStart = Loading::GameplayHandoff::Clock::now();
    bool handoffCompleted = true;
    using Diagnostics::PostLoad::Phase;''')
replace(engine, '''        // update input
        {''', '''        if (mStateManager->getState() == MWBase::StateManager::State_NoGame)
            handoff.cancel();
        const auto handoffResult = handoff.releaseIfPending();
        if (handoffResult != Loading::GameplayHandoff::Result::None)
        {
            const bool settled = handoffResult == Loading::GameplayHandoff::Result::Settled;
            Log(settled ? Debug::Info : Debug::Warning)
                << "Gameplay handoff: " << (settled ? "settled" : "NOT settled (limit/error)")
                << " heldMs=" << handoff.elapsedMs() << " samples=" << handoff.samples()
                << " worstFrameMs=" << handoff.worstMs();
        }
        if (handoff.active())
        {
            // No simulation/time advance while the player's controls are withheld.
            // This is not GM_Loading: MenuMode-sensitive startup scripts still run.
            frametime = 0.f;
            mEnvironment.setFrameDuration(frametime);
        }

        // update input
        {''')
replace(engine, '            mInputManager->update(frametime, false);',
        '            mInputManager->update(frametime, handoff.active(), handoff.active());')
replace(engine, '''            mStateManager->update(frametime);
        }

        bool paused''', '''            mStateManager->update(frametime);
        }
        // A requested save load may have armed the handoff inside update().
        if (handoff.active())
        {
            frametime = 0.f;
            mEnvironment.setFrameDuration(frametime);
        }

        bool paused''')
replace(engine, '''        Log(Debug::Error) << "Error in frame: " << e.what();''',
        '''        handoffCompleted = false;
        Log(Debug::Error) << "Error in frame: " << e.what();''')
replace(engine, '''        mLuaWorker->gc();
    }

    postLoadFrame.mCompleted = true;''', '''        // Do not launch an unmeasured GC job between the settling sample and
        // the input handoff. Normal background GC resumes after cover release.
        if (!handoff.active())
            mLuaWorker->gc();
    }

    if (handoff.active())
    {
        const double elapsed = std::chrono::duration<double, std::milli>(
            Loading::GameplayHandoff::Clock::now() - handoffFrameStart).count();
        handoff.observe(handoffGeneration, elapsed, handoffCompleted, handoff.elapsedMs());
    }
    postLoadFrame.mCompleted = true;''')

screen = 'apps/openmw/mwgui/loadingscreen.cpp'
replace(screen, '#include "loadingscreen.hpp"', '#include "loadingscreen.hpp"\n#include <components/loadinglistener/gameplayhandoff.hpp>')
replace(screen, '    LoadingScreen::~LoadingScreen() {}', '''    LoadingScreen::~LoadingScreen()
    {
        Loading::gameplayHandoff().detachCover(this);
    }''')
replace(screen, '''    void LoadingScreen::setVisible(bool visible)
    {
        WindowBase::setVisible(visible);''', '''    void LoadingScreen::setVisible(bool visible)
    {
        // WindowManager removes the loading GUI mode, enabling normal script
        // and scene updates, but the opaque cover remains until the next safe
        // input boundary. Do not add a GUI mode or pause flag for this cover.
        visible = visible || (mHandoffCover && Loading::gameplayHandoff().active());
        WindowBase::setVisible(visible);''')
replace(screen, '''        mViewer->getSceneData()->dirtyBound();

        setVisible(false);''', '''        mViewer->getSceneData()->dirtyBound();

        mHandoffCover = Loading::gameplayHandoff().active();
        if (mHandoffCover)
        {
            setLabel("Initializing game...", false);
            Loading::gameplayHandoff().attachCover(this, [this] {
                mHandoffCover = false;
                if (mNestedLoadingCount == 0)
                    setVisible(false);
            });
        }
        setVisible(false);''')
replace('apps/openmw/mwgui/loadingscreen.hpp', '        bool mShowWallpaper;',
        '        bool mShowWallpaper;\n        bool mHandoffCover = false;')

state = 'apps/openmw/mwstate/statemanagerimp.cpp'
# Add the include without depending on unrelated include ordering.
replace(state, '#include <components/diagnostics/postload.hpp>',
        '#include <components/diagnostics/postload.hpp>\n#include <components/loadinglistener/gameplayhandoff.hpp>')
replace(state, 'void MWState::StateManager::cleanup(bool force)\n{',
        'void MWState::StateManager::cleanup(bool force)\n{\n    Loading::gameplayHandoff().cancel();')
replace(state, '''        phase("summoned creature cleanup");
        Log(Debug::Info) << "Save load total: "''', '''        phase("summoned creature cleanup");
        MWBase::Environment::get().getWorldModel()->prepareCellLookup();
        phase("prepare lookup index before gameplay");
        if (Loading::gameplayHandoffEnabled())
        {
            Loading::gameplayHandoff().request();
            Log(Debug::Info) << "Gameplay handoff armed: visual cover retained; simulation/input held;"
                                " initial script, render and Lua updates must complete before release";
        }
        Log(Debug::Info) << "Save load total: "''')

header = 'apps/openmw/mwworld/worldmodel.hpp'
replace(header, '        void reportSaveLoadTimings() const;',
        '        void reportSaveLoadTimings() const;\n        void prepareCellLookup();')
replace(header, '        mutable LookupCells mUnindexedLookupCells;',
        '''        mutable LookupCells mUnindexedLookupCells;
        // Only exceptional unloaded cells require exhaustive runtime lookup.
        // Membership is refreshed on dirty notifications, not on each query.
        mutable LookupCells mUnindexedFallbackCells;''')
world = 'apps/openmw/mwworld/worldmodel.cpp'
replace(world, '    mUnindexedLookupCells.clear();',
        '    mUnindexedLookupCells.clear();\n    mUnindexedFallbackCells.clear();')
replace(world, '''void MWWorld::WorldModel::updateCellLookupIndex()
{''', '''void MWWorld::WorldModel::prepareCellLookup()
{
    if (!useWorldCellIndex())
        return;
    Diagnostics::Scope timing("world", "cellCandidates", "prepare");
    updateCellLookupIndex();
}

void MWWorld::WorldModel::updateCellLookupIndex()
{''')
replace(world, '''        if (cell->getState() == CellStore::State_Unloaded)
            continue;
        std::vector<ESM::RefId> ids''', '''        if (cell->getState() == CellStore::State_Unloaded)
        {
            if (cell->getCell()->isEsm4() || cell->hasUnloadedLiveReferences())
                mUnindexedFallbackCells.insert(cell);
            else
                mUnindexedFallbackCells.erase(cell);
            continue;
        }
        std::vector<ESM::RefId> ids''')
replace(world, '        mUnindexedLookupCells.erase(cell);',
        '        mUnindexedLookupCells.erase(cell);\n        mUnindexedFallbackCells.erase(cell);')
replace(world, '''                // A metadata-only CSTA sets hasState() even though it cannot
                // introduce a reference; using hasState() here admitted over
                // 25,000 unrelated saved cells in the user's large modlist.
                // Keep unloaded cells that actually own live runtime refs.
                // ESM4 lacks the ESM3 content index and remains conservative.
                // Loaded/preloaded cells awaiting indexing remain searchable.
                for (CellStore* cell : mUnindexedLookupCells)
                    if (cell->getState() != CellStore::State_Unloaded
                        || cell->getCell()->isEsm4() || cell->hasUnloadedLiveReferences())
                        candidates.push_back(cell);''', '''                // Do not walk tens of thousands of metadata-only cells for
                // every script query. Dirty notifications maintain this small
                // exceptional set; live/preloaded IDs are in the index above.
                candidates.insert(candidates.end(), mUnindexedFallbackCells.begin(),
                    mUnindexedFallbackCells.end());''')

# All validations succeeded. Commit the entire in-memory transaction to disk.
for name, text in sources.items():
    (ROOT / name).write_text(text, encoding='utf-8', newline='\n')
manifest = {'baseline': 'e9476be66251d181d0016a5fd112e1d90e3875f6',
            'files': {n: {'before_blob': EXPECTED[n], 'after_blob': blob(t)} for n, t in sources.items()},
            'edits': edits}
(ROOT / 'loading-repair-source-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print(f'Applied {len(edits)} exact source edits to {len(sources)} verified baseline files.')
