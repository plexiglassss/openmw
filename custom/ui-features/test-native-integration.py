"""Compile actual changed native method bodies against stateful GUI/equipment doubles.
Full MyGUI/OSG rendering remains the Windows build and live-test responsibility.
"""
from pathlib import Path
import subprocess, tempfile, os, sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('.')
def method(path,needle):
 s=(root/path).read_text();start=s.index('{',s.index(needle));end=start+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
sync=method('apps/openmw/mwgui/inventorywindow.cpp','void InventoryWindow::syncPaperDoll(')
refresh=method('components/lua_ui/adapter.cpp','void LuaAdapter::refresh(')
s=(root/'apps/openmw/mwgui/tooltips.cpp').read_text()
start=s.index('            const auto size = mLuaTooltipAdapter->getSize();')
end=s.index('\n        }\n\n        if (!object.hasToolTip',start)
show=s[start:end]
cpp=r'''
#include <algorithm>
#include <array>
#include <vector>
#include <cassert>
#include <iostream>
namespace MyGUI { struct IntSize {int width=0,height=0; IntSize()=default;IntSize(int w,int h):width(w),height(h){} bool operator==(const IntSize&)const=default;}; }
namespace MWWorld {
struct Class;
struct Ptr {int id=0;Ptr()=default;Ptr(int i):id(i){}bool isEmpty()const{return !id;}const Class& getClass()const;bool operator==(const Ptr&)const=default;};
struct InventoryStore {static constexpr int Slots=19;std::array<Ptr,Slots> values;auto end(){return values.end();}auto getSlot(int i){return values[i].isEmpty()?values.end():values.begin()+i;}} inventory;
struct Class {InventoryStore& getInventoryStore(const Ptr&)const{return inventory;}} cls;
const Class& Ptr::getClass()const{return cls;}
}
namespace MWBase {
struct WindowManager {float getScalingFactor(){return 1.5f;}};
struct Environment {static Environment& get(){static Environment e;return e;}WindowManager* getWindowManager(){static WindowManager w;return &w;}};
}
struct Preview {int viewportChanges=0;int getTextureWidth(){return 512;}int getTextureHeight(){return 1024;}void setViewport(int,int){++viewportChanges;}};
struct InventoryWindow {
 MWWorld::Ptr mPtr{1};std::vector<MWWorld::Ptr> mPaperDollEquipment;MyGUI::IntSize mPaperDollViewport;
 Preview preview;Preview* mPreview=&preview;int redraws=0;void dirtyPreview(){++redraws;}
 void syncPaperDoll(int width,int height)
'''+sync+r'''
};
struct Box {bool visible=false;MyGUI::IntSize size;void setVisible(bool v){visible=v;}void changeWidgetSkin(const char*){}void setSize(int w,int h){size={w,h};}MyGUI::IntSize getSize(){return size;}};
struct Container {Box* host;bool visible=false,childVisible=false;void updateCoord(){visible=host->visible;childVisible=visible;}};
struct Adapter {
 Container* mContainer;MyGUI::IntSize getSize(){return {280,230};}void setPosition(int,int){}
 void refresh()
'''+refresh+r'''
};
namespace Settings {struct Game{int mShowOwned=1;};Game& game(){static Game g;return g;}}
struct ToolTip {
 Box host,native;Container container{&host};Adapter adapter{&container};
 Box* mLuaTooltipBox=&host;Box* mDynamicToolTipBox=&native;Adapter* mLuaTooltipAdapter=&adapter;
 bool mLuaTooltipOwned=false;
 MyGUI::IntSize show(bool isOwned){
'''+show+r'''
 return {};
 }
};
int main(){
 InventoryWindow w;
 w.syncPaperDoll(180,360);assert(w.redraws==1&&w.preview.viewportChanges==1);
 for(int i=0;i<120;++i)w.syncPaperDoll(180,360);
 assert(w.redraws==1&&w.preview.viewportChanges==1);
 // Simulate an asynchronous equip completing after the original click/frame.
 MWWorld::inventory.values[0]=MWWorld::Ptr(2);w.syncPaperDoll(180,360);assert(w.redraws==2);
 MWWorld::inventory.values[0]={};w.syncPaperDoll(180,360);assert(w.redraws==3);
 MWWorld::inventory.values[1]=MWWorld::Ptr(3);w.syncPaperDoll(180,360);assert(w.redraws==4);
 w.syncPaperDoll(240,480);assert(w.redraws==4&&w.preview.viewportChanges==2);
 assert(w.mPaperDollViewport.width==360&&w.mPaperDollViewport.height==720);
 w.syncPaperDoll(900,1800);assert(w.mPaperDollViewport.width==512&&w.mPaperDollViewport.height==1024);
 w.mPtr={};w.syncPaperDoll(100,200);assert(w.redraws==4);
 ToolTip tip;
 tip.container.updateCoord();assert(!tip.container.childVisible); // attached to hidden host
 tip.host.setVisible(true);assert(!tip.container.childVisible); // reproduces empty frame
 tip.host.setVisible(false);tip.native.setVisible(true);
 auto size=tip.show(true);assert(size.width==288&&size.height==238);
 assert(tip.container.childVisible&&!tip.native.visible&&tip.mLuaTooltipOwned);
 tip.host.setVisible(false);tip.container.updateCoord();tip.show(false);
 assert(tip.container.childVisible&&!tip.mLuaTooltipOwned);
 std::cout<<"PASS: actual native methods refresh after delayed equip/unequip, avoid unchanged redraws, resize viewport, and restore hidden tooltip descendants\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'native-ui.cpp';p.write_text(cpp);out=Path(tmp)/('native-ui.exe' if os.name=='nt' else 'native-ui')
 if os.name=='nt':cmd=['cl','/nologo','/std:c++20','/EHsc',str(p),'/Fe:'+str(out),'/Fo:'+str(Path(tmp)/'native-ui.obj')]
 else:cmd=['c++','-std=c++20','-Wall','-Wextra','-Werror',str(p),'-o',str(out)]
 subprocess.run(cmd,check=True);subprocess.run([str(out)],check=True)
