"""Execute the production map-refresh methods with counted world/widget services.

The companion MapIconVisibilityTest uses real MyGUI to verify render-item order.
This fixture tests the production lifecycle and input handlers without a game
installation; its substituted renderer does not claim to test actual pixels.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = "apps/openmw/mwgui/mapwindow.cpp"


def extract(source, signature):
    start = source.index(signature)
    begin = source.index("{", start)
    end, depth = begin + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREFIX = r'''
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
int redrawCalls = 0;
bool renderItemsDirty = false;
namespace MyGUI {
struct IntPoint { int left=0, top=0; bool operator==(const IntPoint&) const = default; };
IntPoint operator+(IntPoint a, IntPoint b) { return {a.left+b.left,a.top+b.top}; }
IntPoint operator-(IntPoint a, IntPoint b) { return {a.left-b.left,a.top-b.top}; }
struct IntRect {
    int left=-1,top=-1,right=1,bottom=1;
    int width()const{return right-left;} int height()const{return bottom-top;}
    bool inside(IntPoint p)const{return p.left>=left&&p.left<=right&&p.top>=top&&p.top<=bottom;}
};
enum class MouseButton { Left, Right };
struct Widget {
    bool visible=true,cropped=false; int hides=0; void* data=nullptr;
    IntPoint position,offset;
    virtual ~Widget()=default;
    void setVisible(bool value){visible=value;if(!value)++hides;}
    bool getVisible()const{return visible;}
    void setPosition(IntPoint value){position=value;}
    void setRenderItemTexture(void*){}
    Widget* getParent(){return this;}
    void _updateChilds(){++redrawCalls;renderItemsDirty=false;}
    template<class T>T* getUserData(){return static_cast<T*>(data);}
    void setViewOffset(IntPoint value){offset=value;}
    IntPoint getViewOffset()const{return offset;}
};
}
namespace ESM {
struct StringRefId {};
struct RefId {
    std::string value;
    template<class T>bool is()const{return true;}
    const std::string& getRefIdString()const{return value;}
};
struct Cell {static constexpr int sDefaultWorldspaceId=0;};
struct ExteriorCellLocation {int mX,mY,mWorldspace;};
}
namespace MWBase {struct World;}
namespace MWWorld {
struct Cell {
    std::string name; bool exterior=false; int x=0,y=0;
    bool isExterior()const{return exterior;} bool isQuasiExterior()const{return false;}
    const std::string& getNameId()const{return name;}
    int getGridX()const{return x;} int getGridY()const{return y;}
};
struct CellStore {std::string name;int x=0,y=0;bool exterior=false;};
struct WorldModel {
    std::map<std::string,CellStore> interiors;
    std::map<std::pair<int,int>,CellStore> exteriors;
    CellStore& getInterior(const std::string& name){auto& c=interiors[name];c.name=name;return c;}
    CellStore& getExterior(ESM::ExteriorCellLocation pos){auto& c=exteriors[{pos.mX,pos.mY}];c.x=pos.mX;c.y=pos.mY;c.exterior=true;return c;}
};
}
namespace MWBase {
struct World {
    struct DoorMarker { ESM::RefId dest,doorId;std::string name;float x=0,y=0;bool enabled=true,deleted=false;};
    std::map<std::string,std::vector<DoorMarker>> interiors;
    std::map<std::pair<int,int>,std::vector<DoorMarker>> exteriors;
    int queries=0;
    void getDoorMarkers(MWWorld::CellStore& cell,std::vector<DoorMarker>& out){
        ++queries;const auto& values=cell.exterior?exteriors[{cell.x,cell.y}]:interiors[cell.name];
        // Substitute the already-filtered World service; binary plugin reading
        // and script execution are outside this fixture.
        for(const auto& door:values)if(door.enabled&&!door.deleted)out.push_back(door);
    }
};
struct Environment {
    World world;MWWorld::WorldModel model;
    static Environment& get(){static Environment value;return value;}
    World* getWorld(){return &world;} MWWorld::WorldModel* getWorldModel(){return &model;}
};
}
namespace Constants {constexpr int CellGridRadius=1;}
namespace Settings {struct Map {bool mGlobal=false;};Map& map(){static Map value;return value;}}
namespace MWGui {
MyGUI::IntRect createRect(MyGUI::IntPoint p,int r){return {p.left-r,p.top-r,p.left+r,p.top+r};}
int getLocalViewingDistance(){return 1;}
bool widgetCropped(MyGUI::Widget* widget,MyGUI::Widget*){return widget->cropped;}
struct MarkerData {int cellX=0,cellY=0;float nX=0,nY=0;std::vector<std::string> notes;std::string caption;std::size_t doorIcon=0;};
struct MapImageMarker:MyGUI::Widget {MarkerData marker;int subscriptions=0;MapImageMarker(){data=&marker;}};
struct CustomMarkerCollection {
    struct Note {std::string mNote;};using Container=std::multimap<int,Note>;
    using RangeType=std::pair<Container::const_iterator,Container::const_iterator>;
    Container notes;RangeType getMarkers(const ESM::RefId&)const{return {notes.begin(),notes.end()};}
};
struct MapRenderer {
    MyGUI::IntRect getInteriorGrid()const{return {0,0,0,0};}
    void removeExteriorCell(int,int){}
};
struct LocalMapBase {
    using MarkerUserData=MarkerData;
    struct MapEntry {MyGUI::Widget* mMapWidget;MyGUI::Widget* mFogWidget;int mCellX=0,mCellY=0;std::optional<int> mMapTexture,mFogTexture;};
    const MWWorld::Cell* mActiveCell=nullptr;
    bool mNeedDoorMarkersUpdate=false,mInteriorDoorMarkersReady=false;float mMarkerUpdateTimer=0;
    MyGUI::Widget local;MyGUI::Widget* mLocalMap=&local;MapRenderer renderer;MapRenderer* mLocalMapRender=&renderer;
    MyGUI::IntRect mGrid;
    CustomMarkerCollection mCustomMarkers;
    std::vector<MapImageMarker*> mInteriorDoorMarkerWidgets,mExteriorDoorMarkerWidgets,mDoorMarkersToRecycle;
    std::map<std::pair<int,int>,std::vector<MapImageMarker*>> mExteriorDoorsByCell;
    std::vector<MapEntry> mMaps;
    std::vector<std::unique_ptr<MyGUI::Widget>> owned;
    int allocatedMarkers=0;
    struct IconSet {std::size_t classifyDoor(std::string_view,std::string_view,bool){return 1;}} mIconSet;
    void updateDoorMarkers();void setActiveCell(const MWWorld::Cell&);void onFrame(float);void redraw();void refreshDoorIcons();
    std::vector<MapImageMarker*>& currentDoorMarkersWidgets();
    MapImageMarker* createDoorMarker(){auto value=std::make_unique<MapImageMarker>();auto* p=value.get();owned.push_back(std::move(value));++allocatedMarkers;return p;}
    void styleDoorMarker(MapImageMarker*){renderItemsDirty=true;}
    void doorMarkerCreated(MyGUI::Widget* p){++static_cast<MapImageMarker*>(p)->subscriptions;}
    int doorMarkerSize(const MarkerData&)const{return 24;}
    void updateMarkerCoordinates(MyGUI::Widget*,int){}
    MyGUI::IntPoint getMarkerPosition(float x,float y,MarkerData& p){p.cellX=static_cast<int>(std::floor(x/100));p.cellY=static_cast<int>(std::floor(y/100));return {};}
    MyGUI::IntPoint getPosition(int x,int y,float,float)const{return {x*100,y*100};}
    MapEntry& addMapEntry(){auto map=std::make_unique<MyGUI::Widget>();auto fog=std::make_unique<MyGUI::Widget>();mMaps.push_back({map.get(),fog.get()});owned.push_back(std::move(map));owned.push_back(std::move(fog));return mMaps.back();}
    void updateLocalMap(){} void updateCustomMarkers(){redraw();} void updateMagicMarkers(){redraw();} void updateRequiredMaps(){}
};
struct MapWindow:LocalMapBase {
    MyGUI::IntPoint mLastDragPos;MyGUI::Widget global;MyGUI::Widget* mGlobalMap=&global;int globalRefreshes=0;
    void refreshEntranceWidgets(){++globalRefreshes;}
    void onMouseDrag(MyGUI::Widget*,int,int,MyGUI::MouseButton);void shiftMap(int,int);
};
'''

SUFFIX = r'''
}
int failures=0,checks=0;
void require(bool value,const char* name){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<name<<'\n';}}
MWBase::World::DoorMarker door(const std::string& name,int x=0,int y=0){return {{name},{"door"},name,float(x*100+10),float(y*100+10),true,false};}
void reset(){MWBase::Environment::get()={};Settings::map().mGlobal=false;redrawCalls=0;renderItemsDirty=false;}
int main(){
    using namespace MWGui;
    reset();
    {
        MapWindow map;MWWorld::Cell cave{"cave"};auto& world=MWBase::Environment::get().world;
        world.interiors["cave"]={door("exit"),door("chamber"),door("disabled"),door("deleted")};
        map.setActiveCell(cave);
        require(world.queries==0,"cell activation defers enumeration until scripts have run");
        world.interiors["cave"][2].enabled=false;world.interiors["cave"][3].deleted=true;
        map.onFrame(0);
        require(map.mInteriorDoorMarkerWidgets.size()==2,"first refresh accepts only World-provided active doors");
        require(!renderItemsDirty,"fresh door artwork has its layer order restored immediately");
        const auto initial=map.mInteriorDoorMarkerWidgets;
        int hides=0;for(auto* p:initial)hides+=p->hides;
        for(int i=0;i<1000;++i){map.onMouseDrag(initial[0],i,i,MyGUI::MouseButton::Left);map.onFrame(0);}
        for(int i=0;i<10;++i){map.shiftMap(0,-1);map.onFrame(0);}
        require(world.queries==1,"1000 drag frames and controller pans never rescan the same interior");
        require(map.allocatedMarkers==2&&map.mInteriorDoorMarkerWidgets==initial,"dragging retains exactly the original marker widgets");
        int afterHides=0;for(auto* p:initial)afterHides+=p->hides;
        require(afterHides==hides,"pressed marker is never hidden during interior drag");
        require(std::all_of(initial.begin(),initial.end(),[](auto* p){return p->subscriptions==1;}),"marker input handlers are registered once");
        const int before=redrawCalls;map.refreshDoorIcons();
        require(redrawCalls>before&&!renderItemsDirty,"settings artwork changes restore layers in the same refresh");
        const int queries=world.queries;map.setActiveCell(cave);map.onFrame(0);
        require(world.queries==queries,"same-cell activation keeps the initialized marker set");
        MWWorld::Cell next{"next"};world.interiors["next"]={door("new exit")};
        map.setActiveCell(next);map.onFrame(0);
        require(world.queries==queries+1&&map.mInteriorDoorMarkerWidgets.size()==1,"changing interior rebuilds its own door set");
        require(map.mInteriorDoorMarkerWidgets.front()->marker.caption=="new exit","recycled marker receives the new destination");
        require(map.allocatedMarkers==2&&map.mDoorMarkersToRecycle.size()==1,"cell change reuses existing markers and hides unused ones");
        require(std::all_of(map.mDoorMarkersToRecycle.begin(),map.mDoorMarkersToRecycle.end(),[](auto* p){return !p->visible;}),"retired door markers remain invisible");
    }
    reset();
    {
        MapWindow map;MWWorld::Cell empty{"empty"};auto& world=MWBase::Environment::get().world;
        map.setActiveCell(empty);map.onFrame(0);
        for(int i=0;i<1000;++i){map.onMouseDrag(nullptr,i,i,MyGUI::MouseButton::Left);map.onFrame(0);}
        require(world.queries==1&&map.allocatedMarkers==0,"empty interior is initialized once without perpetual rescanning");
    }
    reset();
    {
        MapWindow map;MWWorld::Cell exterior{"outside",true};auto& world=MWBase::Environment::get().world;
        world.exteriors[{0,0}]={door("cave",0,0)};world.exteriors[{1,0}]={door("tomb",1,0)};
        map.setActiveCell(exterior);
        for(auto& entry:map.mMaps)entry.mMapWidget->visible=entry.mCellX==0&&entry.mCellY==0;
        map.onFrame(0);const auto first=map.mExteriorDoorMarkerWidgets;
        for(int i=0;i<100;++i){map.onMouseDrag(nullptr,i,0,MyGUI::MouseButton::Left);map.onFrame(0);}
        require(world.queries==1&&map.mExteriorDoorMarkerWidgets==first,"exterior viewport keeps already indexed doors");
        for(auto& entry:map.mMaps)if(entry.mCellX==1&&entry.mCellY==0)entry.mMapWidget->visible=true;
        map.shiftMap(-1,0);map.onFrame(0);
        require(world.queries==2&&map.mExteriorDoorMarkerWidgets.size()==2,"exterior pan still discovers a newly visible tile");
        require(!renderItemsDirty,"new exterior door artwork is also layered before rendering");
        Settings::map().mGlobal=true;const int queries=world.queries;
        map.onMouseDrag(nullptr,200,100,MyGUI::MouseButton::Left);map.shiftMap(1,0);map.onFrame(0);
        require(map.globalRefreshes==2&&world.queries==queries,"world-map panning preserves its separate entrance refresh");
    }
    std::cout<<"Checks: "<<checks<<"; failures: "<<failures<<'\n';
    return failures?1:0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-ref", help="Git revision to test, instead of working-tree source")
    parser.add_argument("--compiler", help="C++ compiler executable (g++, clang++, or cl)")
    parser.add_argument("--output", type=Path, default=Path("map-icon-visibility-tests.json"))
    args = parser.parse_args()
    source = (subprocess.check_output(["git", "show", f"{args.source_ref}:{SOURCE}"], cwd=ROOT).decode()
              if args.source_ref else (ROOT / SOURCE).read_text(encoding="utf-8"))
    methods = [
        "    void LocalMapBase::setActiveCell(",
        "    void LocalMapBase::updateDoorMarkers()",
        "    void LocalMapBase::refreshDoorIcons()",
        "    void LocalMapBase::onFrame(",
        "    void LocalMapBase::redraw()",
        "    std::vector<MapImageMarker*>& LocalMapBase::currentDoorMarkersWidgets()",
        "    void MapWindow::onMouseDrag(",
        "    void MapWindow::shiftMap(",
    ]
    code = PREFIX + "\n".join(extract(source, method) for method in methods) + SUFFIX
    compiler = args.compiler or os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++") or shutil.which("cl")
    if not compiler:
        parser.error("A C++20 compiler is required")
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        fixture, executable = directory / "map-refresh.cpp", directory / ("map-refresh.exe" if os.name == "nt" else "map-refresh")
        fixture.write_text(code, encoding="utf-8")
        if Path(compiler).stem.lower() == "cl":
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", str(fixture), f"/Fe{executable}", f"/Fo{directory / 'map-refresh.obj'}"]
        else:
            command = [compiler, "-std=c++20", "-O2", str(fixture), "-o", str(executable)]
        compile_result = subprocess.run(command, text=True, capture_output=True)
        run = (subprocess.run([str(executable)], text=True, capture_output=True)
               if compile_result.returncode == 0 else None)
        result = {
            "result": "PASS" if run is not None and run.returncode == 0 else "FAIL",
            "source": SOURCE, "source_ref": args.source_ref or "working-tree",
            "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
            "fixture_sha256": hashlib.sha256(code.encode()).hexdigest(),
            "production_methods": methods,
            "compiler": compiler, "compile_exit": compile_result.returncode,
            "compile_stdout": compile_result.stdout, "compile_stderr": compile_result.stderr,
            "run_exit": run.returncode if run else None,
            "stdout": run.stdout if run else "", "stderr": run.stderr if run else "",
            "scope": "Production lifecycle and input-handler bodies with counted substitute world/widget services; real MyGUI layer order has a separate native test.",
            "live_gameplay_tested": False,
        }
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(compile_result.stdout + compile_result.stderr, end="")
        if run:
            print(run.stdout + run.stderr, end="")
        raise SystemExit(0 if result["result"] == "PASS" else 1)


if __name__ == "__main__":
    main()
