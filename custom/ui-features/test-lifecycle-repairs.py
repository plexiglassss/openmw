"""Compile the production loading guard and WGL teardown hook against small stateful doubles."""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('.')
def block(s,needle):
 a=s.index(needle);b=s.index('{',a);i=b+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[b:i]
loading=(root/'apps/openmw/mwgui/loadingscreen.cpp').read_text()
hooks=(root/'custom/frame-generation/src/hooks.cpp').read_text()
snapshot=block(loading,'void operator()(osg::RenderInfo& renderInfo) const override')
shutdown=block(hooks,'BOOL WINAPI hookWglDeleteContext(HGLRC context)')
cpp='''
#include <cassert>
using BOOL=bool;using HGLRC=int;using HDC=int;
constexpr int nullptrValue=0;
int current=0,dc=0,game=1,joins=0,deletes=0,logs=0;bool canBind=true;
int wglGetCurrentContext(){return current;}int wglGetCurrentDC(){return dc;}
bool wglMakeCurrent(int d,int c){if(c&&!canBind)return false;dc=d;current=c;return true;}
int GetLastError(){return 5;}void Log(const char*,int){++logs;}
namespace plat::win{int gameContext(){return game;}int gameDC(){return 11;}}
namespace fg{void ShutdownIfActive(){assert(current==game);++joins;game=0;}}
bool real_wglDeleteContext(int c){++deletes;assert(current!=c||!canBind);return true;}
bool hookWglDeleteContext(int context)
'''+shutdown.replace('nullptr','nullptrValue')+'''
namespace osg {
struct Viewport {int width()const{return 100;}int height()const{return 200;}};
struct Camera {Viewport v;const Viewport* getViewport(){return &v;}};
struct RenderInfo{Camera c;int state;Camera* getCurrentCamera(){return &c;}int* getState(){return &state;}};
}
struct Texture{int copies=0;void copyTexImage2D(int&,int,int,int w,int h){assert(w==100&&h==200);++copies;}};
struct Snapshot{mutable bool mOneshot=true;Texture* mTexture;void operator()(osg::RenderInfo& renderInfo)const
'''+snapshot+'''
};
int main(){
 Texture t;Snapshot s{true,&t};osg::RenderInfo r;
 s(r);s(r);s(r);assert(t.copies==1);s.mOneshot=true;s(r);assert(t.copies==2);
 current=1;dc=11;assert(hookWglDeleteContext(1));assert(joins==1&&current==0&&deletes==1);
 game=1;current=2;dc=22;assert(hookWglDeleteContext(1));assert(joins==2&&current==2&&dc==22);
 game=1;current=0;dc=0;assert(hookWglDeleteContext(3));assert(joins==2&&game==1);
 canBind=false;assert(hookWglDeleteContext(1));assert(joins==2&&logs==1);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(cpp)
 subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: one loading snapshot per episode; FG cleanup before deleting owned context, restore unrelated context, ignore other contexts, report acquisition failure')
