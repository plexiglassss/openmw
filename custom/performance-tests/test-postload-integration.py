"""Compile the actual engine log adapter and guard successful-load-only hooks."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
engine = (root / 'apps/openmw/engine.cpp').read_text(encoding='utf-8')
state = (root / 'apps/openmw/mwstate/statemanagerimp.cpp').read_text(encoding='utf-8')
cells = (root / 'apps/openmw/mwworld/cellstore.cpp').read_text(encoding='utf-8')
worker = (root / 'apps/openmw/mwlua/worker.cpp').read_text(encoding='utf-8')

# Keep lifecycle integration outside the generic recorder test: failed loads
# must never arm, cleanup must cancel, already loaded cells must not count.
load = state[state.index('void MWState::StateManager::loadGame('):]
assert load.count('PostLoad::capture().arm()') == 1
assert load.index('Save load total:') < load.index('PostLoad::capture().arm()') < load.index('catch (')
cleanup = state[state.index('void MWState::StateManager::cleanup('):]
assert cleanup.index('PostLoad::capture().cancel()') < cleanup.index('if (mState != State_NoGame')
cell_load = cells[cells.index('void CellStore::load()'):]
assert cell_load.index('if (mState != State_Loaded)') < cell_load.index('PostLoad::CellLoadScope') < cell_load.index('mState = State_Loaded')
update = worker[worker.index('void Worker::update('):worker.index('void Worker::run(')]
assert update.index('Phase::LuaExecution') < update.index('mManager.update()')

helper = engine[engine.index('    void reportPostLoadFrame('):engine.index('    void checkSDLError(')]
fixture = r'''
#include <components/diagnostics/postload.hpp>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace Debug { enum Level { Info }; }
std::vector<std::string> messages;
struct Log {
    std::ostringstream text;
    explicit Log(Debug::Level) {}
    template <class T> Log& operator<<(const T& value) { text << value; return *this; }
    ~Log() { messages.push_back(text.str()); }
};
'''
fixture += helper
fixture += r'''
void require(bool value) { if (!value) throw std::runtime_error("engine log adapter failed"); }
int main() {
    using namespace Diagnostics::PostLoad;
    capture().cancel();
    { PostLoadFrameReport frame; }
    require(messages.empty());
    {
        PostLoadFrameReport frame;
        capture().arm(Clock::now() - std::chrono::seconds(20));
        { Scope phase(Phase::MWScript); CellLoadScope cell; }
        frame.mCompleted = true;
    }
    require(messages.size() == 2);
    require(messages[0].find("partial=1 completed=1") != std::string::npos);
    require(messages[0].find("mwscriptAndTimeMs=") != std::string::npos);
    require(messages[0].find("/cellCalls=1/outerCellMs=") != std::string::npos);
    require(messages[1].find("not a GPU presentation or input-responsiveness") != std::string::npos);
    for (int i = 0; i < 4; ++i) { PostLoadFrameReport frame; frame.mCompleted = true; }
    require(!capture().active());
    require(messages.size() == 7);
    require(messages.back().find("Post-load summary:") != std::string::npos);
    require(messages.back().find("frames=5 completed=5") != std::string::npos);
    require(messages.back().find("Lua execution can overlap rendering") != std::string::npos);
    require(messages.back().find("/cellCalls=1/outerCellMs=") != std::string::npos);
    std::cout << "Post-load engine log integration passed\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    cpp = tmp / 'postload-integration.cpp'
    cpp.write_text(fixture, encoding='utf-8')
    exe = tmp / ('postload-integration.exe' if os.name == 'nt' else 'postload-integration')
    if os.name == 'nt':
        compiler = shutil.which('cl')
        assert compiler, 'Run in an MSVC environment'
        args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', f'/I{root}', str(cpp), f'/Fe:{exe}']
    else:
        args = ['g++', '-std=c++20', '-pthread', '-Wall', '-Wextra', '-Werror', f'-I{root}', str(cpp), '-o', str(exe)]
    subprocess.run(args, cwd=tmp, check=True)
    subprocess.run([str(exe)], cwd=tmp, check=True)
print('Post-load lifecycle hooks passed')
