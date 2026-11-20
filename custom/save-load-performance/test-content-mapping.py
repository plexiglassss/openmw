"""Run the production plugin-mapping method with a minimal reader/world boundary."""
from pathlib import Path
import json
import hashlib
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'apps/openmw/mwstate/statemanagerimp.cpp').read_text()
signature = 'std::map<int, int> MWState::StateManager::buildContentFileIndexMap'
start = source.index(signature)
begin = source.index('{', start)
end, depth = begin + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[start:end]
code = r'''
#include <components/misc/strings/algorithm.hpp>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <cassert>
#include <chrono>
#include <iostream>
namespace ESM {
struct Header {struct MasterData {std::string name;};};
struct ESMReader {std::vector<Header::MasterData> files;const auto& getGameFiles()const{return files;}};
}
namespace MWBase {
struct World {std::vector<std::string> current;const auto& getContentFiles()const{return current;}};
struct Environment {World world;static Environment& get(){static Environment e;return e;}World* getWorld(){return &world;}};
}
namespace MWState {struct StateManager {std::map<int,int> buildContentFileIndexMap(const ESM::ESMReader&)const;};}
'''
code += body
code += r'''
std::map<int,int> previous(const ESM::ESMReader& reader,const std::vector<std::string>& current) {
 std::map<int,int> result;
 for(int p=0;p<(int)reader.files.size();++p)
  for(int c=0;c<(int)current.size();++c)
   if(Misc::StringUtils::ciEqual(reader.files[p].name,current[c])){result.emplace(p,c);break;}
 return result;
}
int main(){
 auto& current=MWBase::Environment::get().world.current;
 ESM::ESMReader reader;MWState::StateManager state;
 assert(state.buildContentFileIndexMap(reader).empty());
 current={"Morrowind.esm","A.esp","a.ESP","last.omwaddon"};
 reader.files={{"a.esp"},{"missing.esp"},{"MORROWIND.ESM"},{"last.omwaddon"},{"a.esp"}};
 assert(state.buildContentFileIndexMap(reader)==previous(reader,current));
 assert(state.buildContentFileIndexMap(reader).at(0)==1);
 current.clear();reader.files.clear();
 for(int i=0;i<2104;++i){current.push_back("Plugin "+std::to_string(i)+".esp");reader.files.push_back({"plugin "+std::to_string(2103-i)+".ESP"});}
 const auto a=std::chrono::steady_clock::now();auto expected=previous(reader,current);
 const auto b=std::chrono::steady_clock::now();auto actual=state.buildContentFileIndexMap(reader);
 const auto c=std::chrono::steady_clock::now();assert(actual==expected);
 std::cout<<"PASS: actual plugin mapping; empty, missing, case, duplicate-first and reordered 2104 plugins; previousMs="
 <<std::chrono::duration<double,std::milli>(b-a).count()<<" indexedMs="<<std::chrono::duration<double,std::milli>(c-b).count()<<'\n';
}
'''
with tempfile.TemporaryDirectory() as temp:
    cpp = Path(temp) / 'mapping.cpp'
    exe = Path(temp) / ('mapping.exe' if os.name == 'nt' else 'mapping')
    cpp.write_text(code)
    if os.name == 'nt':
        subprocess.run(['cl', '/nologo', '/std:c++20', '/EHsc', '/O2', '/I' + str(root), str(cpp), '/Fe:' + str(exe)], cwd=temp, check=True)
    else:
        subprocess.run(['g++', '-std=c++20', '-O2', '-I', str(root), str(cpp), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
    print(result.stdout, end='')
Path('save-load-content-mapping.json').write_text(json.dumps({
    'result': 'PASS', 'production_method_sha256': hashlib.sha256(body.encode()).hexdigest(),
    'scope': 'Actual content mapping method, stub reader/world; synthetic input, not whole game timings',
    'output': result.stdout.strip(), 'gameplay_tested': False,
}, indent=2) + '\n')
