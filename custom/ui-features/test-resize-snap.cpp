#include "components/widgets/snaprect.hpp"
#include <cassert>
int main(){
 using namespace Gui;
 std::vector<SnapRect> others{{400,100,200,300}};
 auto r=snapResizeRect({100,100,291,200},others,1920,1080,12,2,200,60);
 assert(r.left==100 && r.right()==400 && r.top==100 && r.height==200);
 r=snapResizeRect({608,100,292,200},others,1920,1080,12,1,200,60);
 assert(r.left==600 && r.right()==900);
 r=snapResizeRect({100,108,292,283},others,1920,1080,12,10,200,60);
 assert(r.right()==400 && r.bottom()==400 && r.top==108);
 r=snapResizeRect({100,100,291,200},others,1920,1080,12,2,310,60);
 assert(r.width==291); // refuses a snap that would violate the caller minimum
 r=snapResizeRect({100,100,280,200},others,1920,1080,12,2,200,60);
 assert(r.width==280); // outside threshold
 r=snapResizeRect({8,9,400,300},{},1920,1080,12,5,200,60);
 assert(r.left==0 && r.top==0 && r.right()==408 && r.bottom()==309);
}
