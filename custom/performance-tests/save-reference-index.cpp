#include <components/misc/transientrefindex.hpp>
#include <cassert>
#include <chrono>
#include <iostream>
#include <list>
#include <random>
#include <utility>

using Key = std::pair<int, int>;
struct Ref { Key key; int state; };
int main()
{
    const auto keyOf = [](const Ref& r) { return r.key; };
    std::list<Ref> refs{{{1, 10}, 5}, {{1, 11}, 6}, {{1, 10}, 7}};
    auto linear = [&](Key key) -> Ref* {
        for (auto& r : refs) if (r.key == key) return &r;
        return nullptr;
    };
    Misc::TransientRefIndex<Key, Ref> index;
    assert(index.find(refs, {1, 10}, keyOf) == &refs.front());
    assert(index.find(refs, {1, 11}, keyOf) == linear({1, 11}));
    assert(index.find(refs, {1, 10}, keyOf) == &refs.front()); // first duplicate wins
    assert(index.find(refs, {1, 12}, keyOf) == nullptr); // same refnum, different base
    refs.push_back({{2, 20}, 9});index.inserted(refs.back().key, &refs.back());
    assert(index.find(refs, {2, 20}, keyOf) == &refs.back());
    refs.push_back({{1, 10}, 99});index.inserted(refs.back().key, &refs.back());
    assert(index.find(refs, {1, 10}, keyOf) == &refs.front());
    refs.front().state=42;assert(index.find(refs, {1, 10}, keyOf)->state==42);
    Misc::TransientRefIndex<Key, Ref> empty;
    std::list<Ref> none;
    assert(!empty.find(none, {0,0}, keyOf));assert(!empty.find(none, {0,0}, keyOf));
    none.push_back({{0,0}, 1});empty.inserted(none.back().key,&none.back());
    assert(empty.find(none,{0,0},keyOf)==&none.back());
    // Deterministic mixed misses, duplicate identities, base replacements and appends.
    std::mt19937 rng(42);
    for(int i=0;i<3000;++i) {
        Key key{int(rng()%250),int(rng()%3)};
        assert(index.find(refs,key,keyOf)==linear(key));
        if(i%4==0) { refs.push_back({key,i});index.inserted(key,&refs.back()); }
    }
    // Operation counts demonstrate the removed repeated full-list traversal.
    std::list<Ref> large;
    for(int i=0;i<10000;++i) large.push_back({{i,1},i});
    Misc::TransientRefIndex<Key,Ref> fast;
    int visits=0;
    auto counted=[&](const Ref& r){++visits;return r.key;};
    for(int i=9999;i>=5000;--i) assert(fast.find(large,{i,1},counted)->state==i);
    assert(visits==89972); // eight scans + one build; no later list scans
    std::cout << "PASS: exact first-match semantics, base-ID validation, appends, mutable state, misses, mixed replay; 5000 lookups visit 89972 nodes instead of 37502500\n";
}
