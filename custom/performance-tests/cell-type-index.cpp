#include <components/misc/celltypeindex.hpp>

#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr unsigned door = 1;
    constexpr unsigned npc = 2;
    constexpr unsigned scenery = 3;

    void require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    void conservativeLifecycle()
    {
        Misc::CellTypeIndex<const int*> index;
        int empty = 0, doors = 0, mixed = 0, unknown = 0, unindexed = 0;
        index.addCell(&empty);
        index.addCandidate(&doors, door);
        index.addCandidate(&doors, door);
        index.addCandidate(&mixed, npc);
        index.addCandidate(&mixed, scenery);
        index.addCandidate(&unknown, 0);
        require(index.mayContain(&empty, door), "partial scan must not prove absence");
        index.complete();
        require(!index.mayContain(&empty, door), "known empty cell");
        require(index.mayContain(&doors, door), "matching type must load");
        require(!index.mayContain(&doors, npc), "nonmatching type");
        require(index.mayContain(&mixed, npc) && index.mayContain(&mixed, scenery), "type union");
        require(index.mayContain(&unknown, door) && index.mayContain(&unknown, npc), "unknown ID fallback");
        require(index.mayContain(&unindexed, door), "dynamic/draft cell fallback");
        require(index.mayContain(&empty, 0), "unknown query fallback");
        for (unsigned i = 0; i != 10; ++i)
            require(!index.mayContain(&doors, npc), "repeated negative query changed state");

        index.observeTypeChange(0, npc); // new generated record, or an already-unknown base
        index.observeTypeChange(door, door); // saved override preserving record type
        require(!index.mayContain(&doors, npc), "same-type/new records must retain optimization");
        index.observeTypeChange(door, npc);
        require(index.mayContain(&doors, npc), "cross-type override must invalidate negatives");
        index.observeTypeChange(npc, npc);
        require(index.mayContain(&doors, npc), "later same-type observation must not erase invalidation");
        index.resetTypeCompatibility(); // only after authoritative map reset/rebuild
        require(!index.mayContain(&doors, npc), "clear/reload restored static type map");
        index.observeTypeChange(door, 0);
        require(index.mayContain(&empty, npc), "unknown changed type must fall back");
    }

    struct Definition
    {
        unsigned identity;
        unsigned type;
        bool deleted;
    };

    void overrideSuperset()
    {
        // Fixture oracle resolves winning identities independently of the
        // production index, which only receives raw candidate types. It covers
        // replacement, deletion, revival and distinct leased destination refs.
        const std::vector<std::vector<Definition>> histories = {
            {}, {{1, scenery, false}}, {{1, door, false}},
            {{1, door, false}, {1, door, true}},
            {{1, door, false}, {1, scenery, false}},
            {{1, scenery, false}, {1, npc, false}},
            {{1, door, false}, {1, door, true}, {1, door, false}},
            {{1, scenery, false}, {2, door, false}}, // leased ref appended at destination
            {{1, 0, false}}, {{1, door, true}},
            {{1, door, false}, {2, npc, false}, {1, scenery, false}},
        };
        for (const auto& history : histories)
        {
            Misc::CellTypeIndex<unsigned> index;
            index.addCell(7);
            std::map<unsigned, Definition> winners;
            for (const auto& ref : history)
            {
                winners.insert_or_assign(ref.identity, ref);
                if (!ref.deleted)
                    index.addCandidate(7, ref.type);
            }
            index.complete();
            for (const auto& [id, ref] : winners)
            {
                (void)id;
                if (!ref.deleted)
                    require(index.mayContain(7, ref.type), "candidate index hid a winning identity");
            }
        }
        // A losing Door definition deliberately remains a false positive.
        Misc::CellTypeIndex<unsigned> superseded;
        superseded.addCandidate(7, door);
        superseded.addCandidate(7, scenery);
        superseded.complete();
        require(superseded.mayContain(7, door), "unsafe effective-only reduction");
    }
}

int main()
{
    conservativeLifecycle();
    overrideSuperset();
    std::cout << "PASS: production cell type index lifecycle, unknown/canonical-cell fallback, type-map invalidation, "
                 "and conservative override identity fixtures\n";
}
