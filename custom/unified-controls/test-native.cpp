#include <components/misc/dialoguetext.hpp>
#include <components/misc/bindingcapture.hpp>
#include <cassert>
#include <iostream>
using namespace Misc::DialogueText;
void check(std::string_view value, Category expected)
{
    const auto spans=classify(value,Context::Dialogue);
    assert(!spans.empty());
    for (auto span:spans) assert(span.mCategory==expected);
}
int main()
{
    check("Warehouse Shipping Log has been removed from your inventory.",Category::InventoryRemoval);
    check("Surfeit of Thieves has been added to your inventory.",Category::Inventory);
    check("3 notes have been removed from your inventory.",Category::InventoryRemoval);
    check("Item removed: A note.",Category::InventoryRemoval);
    check("Item received: Removed Pages.",Category::Inventory);
    check("I said a note has been removed from your inventory.",Category::Default);
    check("If a note has been removed from your inventory, tell me.",Category::Default);
    check("Items removed:",Category::Default);
    Misc::BindingCapture capture;
    assert(!capture.event(0,4,true));
    capture.begin();
    assert(capture.event(0,4,true));
    assert(capture.event(0,4,true)); // repeat
    assert(capture.event(1,1,true)); // unrelated input is swallowed
    assert(capture.event(1,1,false));
    assert(capture.active());
    assert(!capture.take());
    assert(capture.event(0,4,false));
    assert(!capture.active());
    assert(capture.take()==std::make_pair(0,4));
    assert(!capture.take());
    assert(!capture.event(0,4,false));
    capture.begin(); capture.event(2,0,true); capture.event(2,0,false);
    assert(capture.take()==std::make_pair(2,0));
    capture.begin(); capture.event(0,41,true); capture.cancel();
    assert(!capture.take() && !capture.active());
    std::cout<<"Removal classification and exclusive binding capture: PASS\n";
}
