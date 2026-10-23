#include <array>
#include <components/widgets/snaprect.hpp>
#include <cstdlib>
#include <iostream>
void check(bool value)
{
    if (!value)
        std::abort();
}
int main()
{
    const std::array<Gui::SnapRect, 1> target{ { { 100, 100, 200, 200 } } };
    auto snap = [&](Gui::SnapRect p) { return Gui::snapRect(p, target, 1920, 1080, 12); };
    check(snap({ 309, 105, 200, 200 }).left == 300); // adjacent, top aligned
    check(snap({ 309, 105, 200, 200 }).top == 100);
    check(snap({ 313, 105, 200, 200 }).left == 313); // release beyond threshold
    check(snap({ 309, 600, 200, 200 }).left == 309); // no distant magnetic column
    check(snap({ 105, 309, 200, 200 }).top == 300);
    check(snap({ 5, 600, 200, 200 }).left == 0);
    check(snap({ 1714, 600, 200, 200 }).left == 1720);
    check(snap({ 600, 874, 200, 200 }).top == 880);
    check(snap({ 400, 400, 200, 200 }).width == 200); // drag never resizes
    // Raw pointer coordinate advances independently of snapped display coordinate.
    for (int raw = 300; raw <= 325; ++raw)
        check(snap({ raw, 100, 200, 200 }).left == (raw <= 312 ? 300 : raw));
    std::cout
        << "PASS: adjacency, alignment, threshold release, distant-window exclusion, screen edges, size preservation\n";
}
