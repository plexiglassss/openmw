#include <components/loadinglistener/gameplayhandoff.hpp>
#include <cassert>
#include <iostream>
#include <limits>

int main()
{
    using H = Loading::GameplayHandoff;
    H h;
    int owner = 0, releases = 0;
    auto attach = [&] { h.attachCover(&owner, [&] { ++releases; }); };
    h.request(); attach();
    auto generation = h.generation();
    for (double ms : {8250., 1860., 17., 17.})
        h.observe(generation, ms, true, ms);
    assert(h.active() && h.result() == H::Result::None && releases == 0);
    h.observe(generation, 17, true, 10200);
    assert(h.result() == H::Result::Settled && h.active() && releases == 0);
    assert(h.releaseIfPending() == H::Result::Settled && !h.active() && releases == 1);
    assert(h.releaseIfPending() == H::Result::None && releases == 1);

    h.request(); attach(); generation = h.generation();
    for (double ms : {17., 17., 9000., 17., 17.})
        h.observe(generation, ms, true, 9100);
    assert(h.result() == H::Result::None);
    h.observe(generation, 17, true, 9200);
    assert(h.result() == H::Result::Settled);
    h.cancel(); assert(!h.active() && releases == 2);

    h.request(); attach(); generation = h.generation();
    h.observe(generation, 31000, true, 31000);
    assert(h.result() == H::Result::Limit);
    assert(h.releaseIfPending() == H::Result::Limit && releases == 3);

    h.request(); attach(); generation = h.generation();
    for (unsigned i=0; i<H::MaxSamples; ++i)
        h.observe(generation, 51, true, 1000);
    assert(h.result() == H::Result::Limit);
    h.cancel(); assert(releases == 4);

    h.request(); attach(); generation = h.generation();
    h.observe(generation, 1, false, 1);
    assert(h.result() == H::Result::Error);
    h.cancel(); assert(releases == 5);
    h.request(); attach(); generation = h.generation();
    h.observe(generation, std::numeric_limits<double>::quiet_NaN(), true, 1);
    assert(h.result() == H::Result::Error);
    h.cancel();

    h.request(); attach(); generation = h.generation();
    h.request(); attach();
    for (unsigned i=0; i<10; ++i)
        h.observe(generation, 1, true, 1);
    assert(h.samples() == 0 && h.active());
    h.detachCover(&owner);
    assert(!h.active());
    h.cancel(); // no callback into a destroyed LoadingScreen
    std::cout << "Handoff lifecycle tests passed: slow frames, delayed release, new load, cancellation, limit, error and cover destruction.\n";
}
