#ifndef OPENMW_COMPONENTS_TESTS_MAPMARKERS_CASES_HPP
#define OPENMW_COMPONENTS_TESTS_MAPMARKERS_CASES_HPP

#include <components/mapmarkers/classification.hpp>

#include <array>
#include <string_view>

namespace MapMarkersTest
{
    using MapMarkers::Category;

    struct Case
    {
        std::string_view cellId;
        std::string_view displayName;
        std::string_view doorModel;
        bool placeLabel;
        Category expected;
    };

    // Expected categories include reproduced source-table conflicts and false
    // positives from the earlier Lua implementation, not only keyword examples.
    inline constexpr auto cases = std::to_array<Case>({
        { "", "", "", false, Category::Misc },
        { "my_mod_unidentified_entrance", "Nerun", "", false, Category::Misc },
        { "Addamasartus", "", "", false, Category::Cave },
        { "Samarys Ancestral Tomb", "", "", false, Category::Tomb },
        { "Abaesen-Pulu Mine", "", "", false, Category::Mine },
        { "Arkngthand", "", "", false, Category::Dwemer },
        { "Balmora", "", "", true, Category::City },
        { "Seyda Neen", "", "", true, Category::Town },
        { "Tel Fyr", "", "", true, Category::Settlement },
        { "Fort Frostmoth", "", "", true, Category::Fort },
        { "Ald Sotha", "", "", true, Category::Daedric },
        { "ADDAMASARTUS", "Grotte locale", "", false, Category::Cave },
        { "arkngthand", "Local Name Tomb", "", false, Category::Dwemer },
        { "", "  SAMARYS\t ANCESTRAL TOMB  \n", "", false, Category::Tomb },
        { "", "Unknown-Cave", "", false, Category::Cave },
        { "", "Camp Hope\xEF\xBC\x8C" "Nera\xE2\x80\x99" "s House", "", false, Category::House },
        { "", "Unnamed Azure Caves", "", false, Category::Cave },
        { "", "Qirn Caverns", "", false, Category::Cave },
        { "", "Qirn Grottos", "", false, Category::Cave },
        { "", "Qirn Egg Mine", "x/ex_cave_door_01.nif", false, Category::Mine },
        { "", "Qirn Ancestral Tomb", "", false, Category::Tomb },
        { "", "Qirn Barrow", "", false, Category::Barrow },
        { "", "Qirn Dwarven Ruin", "", false, Category::Dwemer },
        { "", "Qirn Daedric Shrine", "", false, Category::Daedric },
        { "", "Qirn Stronghold", "", false, Category::Stronghold },
        { "", "Qirn Fort", "", false, Category::Fort },
        { "", "Qirn Tower", "", false, Category::Tower },
        { "", "Qirn Ruins", "", false, Category::Ruin },
        { "", "Qirn Camp", "", false, Category::Camp },
        { "", "Qirn Farm", "", true, Category::Settlement },
        { "", "Qirn House", "", false, Category::House },
        { "", "Qirn Ship", "", false, Category::Ship },
        { "", "Qirn Shipwreck", "", false, Category::Shipwreck },
        { "", "Qirn Peak", "", true, Category::Landmark },
        { "", "Campion", "", false, Category::Misc },
        { "", "Minerva", "", false, Category::Misc },
        { "", "Fortunate", "", false, Category::Misc },
        { "", "Barrowdale", "", false, Category::Misc },
        { "", "Shipley", "", false, Category::Misc },
        { "", "Household", "", false, Category::Misc },
        { "", "Tombak", "", false, Category::Misc },
        { "", "Minefield", "", false, Category::Misc },
        { "", "Cavendish", "", false, Category::Misc },
        { "Camp Hope, Nera's House", "Nera's House", "", false, Category::House },
        { "Tomb Town, Blue Inn", "Blue Inn", "", false, Category::House },
        { "Balmora, New Copper Mine", "Balmora", "", false, Category::Mine },
        { "Balmora, Council Chambers", "Balmora", "", false, Category::House },
        { "Balmora", "Balmora, Council Chambers", "", false, Category::House },
        { "Balmora, Guild of Mages", "", "", false, Category::House },
        { "Arkngthand, Hall of Centrifuge", "", "", false, Category::Dwemer },
        { "", "Bodrum, Quarry", "", true, Category::Town },
        { "", "Bodrum, Quarry", "x/ex_cave_door_01.nif", false, Category::Town },
        { "", "Abaelun Mine & Indalen Tomb Entry", "", false, Category::Mine },
        { "", "Elanius Camp & Ienith Tomb", "", false, Category::Camp },
        { "Balmora", "", "x/ex_cave_door_01.nif", false, Category::City },
        { "eluba-addon", "", "", false, Category::Misc },
        { "eluba-addon, exterior", "", "", true, Category::Mine },
        { "eluba-addon grotto", "", "", false, Category::Cave },
        { "ald niripal", "", "", false, Category::Daedric },
        { "ald niripal, exterior", "", "", true, Category::Cave },
        { "molkobal & olms ancestral tomb", "", "", true, Category::Cave },
        { "my_mod_unique", "Qirn", "Meshes\\D\\IN_DAE_DOOR_LOAD_OVAL.NIF", false, Category::Daedric },
        { "my_mod_unique", "Qirn", "x/ex_dae_door_01.nif", false, Category::Daedric },
        { "my_mod_unique", "Qirn", "d/in_dwrv_door00.nif", false, Category::Dwemer },
        { "my_mod_unique", "Qirn", "x/ex_cave_door_01.nif", false, Category::Cave },
        { "my_mod_unique", "Qirn", "x/in_moldcave_door_01.nif", false, Category::Cave },
        { "my_mod_unique", "Qirn", "x/in_lavacave_door_01.nif", false, Category::Cave },
        { "my_mod_unique", "Qirn", "x/in_tomb_door_01.nif", false, Category::Tomb },
        { "my_mod_unique", "Qirn", "x/in_bm_barrowdoor_01.nif", false, Category::Barrow },
        { "my_mod_unique", "Qirn", "x/in_stronghold_door_01.nif", false, Category::Stronghold },
        { "my_mod_unique", "Qirn", "x/in_strong_door_01.nif", false, Category::Stronghold },
        { "my_mod_unique", "Qirn", "x/ex_de_door_01.nif", false, Category::Misc },
        { "my_mod_unique", "Qirn", "x/in_dae_ruin/door.nif", false, Category::Misc },
        { "my_mod_unique", "Qirn", "x/door_wood.nif", false, Category::Misc },
        { "my_mod_unique", "Qirn Village", "x/ex_cave_door_01.nif", true, Category::Settlement },
        { "my_mod_unique", "Qirn", "x/ex_cave_door_01.nif", true, Category::Misc },
    });
}

#endif
