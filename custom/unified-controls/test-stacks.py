"""Compile the actual old/new update bodies against deterministic inventory fixtures."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
name='apps/openmw/mwgui/containeritemmodel.cpp'
def body(source):
 a=source.index('    void ContainerItemModel::update()');b=source.index('    bool ContainerItemModel::onDropItem',a)
 return source[a:b]
old=body(subprocess.check_output(['git','show','520a8bcdff36f519b2fe63a2872de132856a1b90:'+name],cwd=root,text=True)).replace('ContainerItemModel::update','OldModel::update')
new=body((root/name).read_text()).replace('ContainerItemModel::update','NewModel::update')
fixture=r'''
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
namespace ESM { using RefId=std::string; }
struct Ref { std::string id; int count=1; const auto& getRefId()const{return id;} int getCount()const{return count;} };
struct Class;
struct Store;
struct Ptr { Ref ref; int instance, condition=100, charge=100, soul=0; bool script=false,equipped=false,visible=true; Store* store=nullptr; const Class& getClass()const; const Ref& getCellRef()const{return ref;} };
struct Store:std::vector<Ptr>{};
struct Class { Store& getContainerStore(const Ptr&p)const{return *p.store;} bool showsInInventory(const Ptr&p)const{return p.visible;} };
const Class& Ptr::getClass()const{static Class cls;return cls;}
namespace MWWorld { using Ptr=::Ptr; using ContainerStore=::Store; using ContainerStoreIterator=Store::iterator; }
int comparisons=0;
bool stacks(const Ptr&a,const Ptr&b) { ++comparisons;return a.instance==b.instance || (a.ref.id==b.ref.id && !a.script && !b.script && !a.equipped && !b.equipped && a.condition==b.condition && a.charge==b.charge && a.soul==b.soul); }
struct ItemStack { Ptr mBase; int mCount; ItemStack(Ptr p,void*,int n):mBase(p),mCount(n){} };
struct Model { std::vector<ItemStack>mItems;std::vector<std::pair<Ptr,int>>mItemSources;std::vector<Ptr>mWorldItems; };
struct OldModel:Model{void update();};struct NewModel:Model{void update();};
'''
main=r'''
int main(){
 Store a,b;for(int i=0;i<3000;++i){Ptr p{{"record"+std::to_string(i%500),i%9==0?-7:1},i};p.charge=i%7?100:50;p.condition=i%11?100:80;p.soul=i%13==0;p.script=i%29==0;p.equipped=i%31==0;p.visible=i%23!=0;(i%2?a:b).push_back(p);}
 Ptr pa{{"container",1},99999},pb=pa;pa.store=&a;pb.store=&b;
 OldModel old;NewModel newer;old.mItemSources={{pa,0},{pb,0}};newer.mItemSources=old.mItemSources;
 for(int i=0;i<80;++i){Ptr p=a[i];p.instance+=10000;old.mWorldItems.push_back(p);}newer.mWorldItems=old.mWorldItems;
 comparisons=0;old.update();const int before=comparisons;comparisons=0;newer.update();const int after=comparisons;
 assert(old.mItems.size()==newer.mItems.size());
 for(size_t i=0;i<old.mItems.size();++i){assert(old.mItems[i].mBase.instance==newer.mItems[i].mBase.instance);assert(old.mItems[i].mCount==newer.mItems[i].mCount);}
 assert(after<before/10);
 // Every rebuild observes live mutation; there is no cross-frame stale cache.
 a[5].ref.count+=8;a[12].condition=20;a[15].equipped=!a[15].equipped;old.update();newer.update();
 assert(old.mItems.size()==newer.mItems.size());for(size_t i=0;i<old.mItems.size();++i){assert(old.mItems[i].mBase.instance==newer.mItems[i].mBase.instance);assert(old.mItems[i].mCount==newer.mItems[i].mCount);}
 std::cout<<"Stack parity PASS; predicate calls "<<before<<" -> "<<after<<" (synthetic fixture, not game FPS)\n";
}
'''
with tempfile.TemporaryDirectory() as temp:
 path=Path(temp)/'stacks.cpp';path.write_text(fixture+old+new+main)
 exe=Path(temp)/'stacks'
 subprocess.run(['g++','-std=c++20','-O2','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
