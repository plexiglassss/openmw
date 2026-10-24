"""Execute production CSTA read/write control flow with instrumented cell/ESM doubles.
This tests load decisions and metadata round trips, not a full engine installation.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'apps/openmw/mwworld/worldmodel.cpp').read_text()
def function(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

harness = r'''
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
namespace Debug { enum { Warning }; }
struct Log { explicit Log(int) {} template<class T> Log& operator<<(const T&) { return *this; } };
namespace ESM {
constexpr uint32_t REC_CSTA=1;
struct ESMReader { int id=1; bool refs=false, fog=false, skipped=false; int water=17, respawn=42;
 int getCellId() {return id;} bool hasMoreSubs() {return refs;} void skipRecord() {skipped=true;} };
struct ESMWriter { int id=0, water=0, respawn=0; bool fog=false, refs=false, ended=false;
 void startRecord(uint32_t) {} void writeCellId(int value) {id=value;} void endRecord(uint32_t) {ended=true;} };
struct CellState {int mId=0; bool mHasFogOfWar=false; int water=0, respawn=0;
 void load(ESMReader& r) {mHasFogOfWar=r.fog; water=r.water; respawn=r.respawn;}
 void save(ESMWriter& w) {w.water=water; w.respawn=respawn;} };
}
namespace MWWorld {
struct WorldModel;
struct GetCellStoreCallback {explicit GetCellStoreCallback(WorldModel&) {}};
struct CellStore {
 enum State {State_Unloaded, State_Preloaded, State_Loaded};
 State status=State_Unloaded; int loads=0, reads=0, water=0, respawn=0, id=1; bool fog=false, refs=false, hasState=false;
 State getState() const {return status;} void load() {++loads; status=State_Loaded;}
 void loadState(const ESM::CellState& s) {water=s.water; respawn=s.respawn; hasState=true;}
 void readFog(ESM::ESMReader&) {fog=true;}
 void readReferences(ESM::ESMReader& r, GetCellStoreCallback*) {assert(status==State_Loaded); ++reads; refs=r.refs; r.refs=false;}
 void saveState(ESM::CellState& s) {s.mId=id; s.water=water; s.respawn=respawn; s.mHasFogOfWar=fog;}
 void writeFog(ESM::ESMWriter& w) {w.fog=fog;}
 void writeReferences(ESM::ESMWriter& w) {w.refs=refs;}
};
struct WorldModel {
 CellStore cell; bool missing=false; int resolutions=0;
 struct {int cells=0, metadataOnly=0; double resolveMs=0,stateMs=0,baseMs=0,referencesMs=0;} mSaveLoadTimings;
 CellStore* findCell(int, bool force) {++resolutions; if(missing)return nullptr; if(force)cell.load(); return &cell;}
 bool readRecord(ESM::ESMReader&, uint32_t);
 void writeCell(ESM::ESMWriter&, CellStore&) const;
};
}
'''
harness += function('bool MWWorld::WorldModel::readRecord(')
harness += '\nnamespace MWWorld {\n' + function('void MWWorld::WorldModel::writeCell(').replace('MWWorld::WorldModel::', 'WorldModel::') + '\n}\n'
harness += r'''
int main() {
 for (auto initial : {MWWorld::CellStore::State_Unloaded, MWWorld::CellStore::State_Preloaded, MWWorld::CellStore::State_Loaded})
 for (bool fog : {false,true}) for(bool refs : {false,true}) {
  MWWorld::WorldModel world; world.cell.status=initial;
  ESM::ESMReader reader; reader.fog=fog; reader.refs=refs;
  assert(world.readRecord(reader,ESM::REC_CSTA));
  assert(world.cell.hasState && world.cell.water==17 && world.cell.respawn==42 && world.cell.fog==fog);
  assert(world.cell.loads==(refs && initial!=MWWorld::CellStore::State_Loaded ? 1:0));
  assert(world.cell.reads==(refs?1:0)); assert(world.mSaveLoadTimings.metadataOnly==(refs?0:1));
  const int loads=world.cell.loads;
  ESM::ESMWriter writer; world.writeCell(writer,world.cell);
  assert(writer.ended && writer.water==17 && writer.respawn==42 && writer.fog==fog && writer.refs==refs);
  assert(world.cell.loads==loads); // Saving must not force deferred reference loads.
  if(!refs && initial!=MWWorld::CellStore::State_Loaded) {
   world.cell.load(); // Later demand preserves metadata.
   assert(world.cell.water==17 && world.cell.respawn==42 && world.cell.fog==fog);
  }
 }
 MWWorld::WorldModel missing; missing.missing=true; ESM::ESMReader reader;
 assert(missing.readRecord(reader,ESM::REC_CSTA) && reader.skipped);
 assert(!missing.readRecord(reader,99));
 std::cout << "Production CSTA control-flow checks passed (12 state/fog/reference combinations, missing cell, unrelated record).\n";
}
'''
with tempfile.TemporaryDirectory() as d:
    cpp = Path(d)/'test.cpp'; exe=Path(d)/'test'
    cpp.write_text(harness)
    subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
