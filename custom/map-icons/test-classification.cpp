// Standalone production-component tests and discovery-only microbenchmark.
// No engine data, GTest, Lua, image library, or additional third-party dependency.
#include <apps/components_tests/mapmarkers/cases.hpp>
#include <components/mapmarkers/spatialindex.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    bool sCountAllocations = false;
    std::size_t sAllocations = 0;

    void require(bool value, const std::string& message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
}

// Keep the counting hooks visible even when the benchmark uses optimization.
#if defined(_MSC_VER)
#define MAP_MARKERS_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define MAP_MARKERS_TEST_NOINLINE __attribute__((noinline))
#else
#define MAP_MARKERS_TEST_NOINLINE
#endif

MAP_MARKERS_TEST_NOINLINE void* operator new(std::size_t size)
{
    if (sCountAllocations)
        ++sAllocations;
    if (void* result = std::malloc(size == 0 ? 1 : size))
        return result;
    throw std::bad_alloc();
}

MAP_MARKERS_TEST_NOINLINE void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

MAP_MARKERS_TEST_NOINLINE void operator delete(void* value) noexcept
{
    std::free(value);
}

MAP_MARKERS_TEST_NOINLINE void operator delete[](void* value) noexcept
{
    ::operator delete(value);
}

MAP_MARKERS_TEST_NOINLINE void operator delete(void* value, std::size_t) noexcept
{
    ::operator delete(value);
}

MAP_MARKERS_TEST_NOINLINE void operator delete[](void* value, std::size_t) noexcept
{
    ::operator delete(value);
}

#undef MAP_MARKERS_TEST_NOINLINE

int main(int argc, char** argv)
{
    try
    {
        using MapMarkers::Category;
        const std::string path = argc > 1 ? argv[1] : "custom/map-icons/known-locations.tsv";
        const std::size_t iterations = argc > 2 ? static_cast<std::size_t>(std::stoull(argv[2])) : 100000;
        require(iterations > 0, "Benchmark iteration count must be positive");
        for (const auto& value : MapMarkersTest::cases)
            require(MapMarkers::classify(value.cellId, value.displayName, value.doorModel, value.placeLabel)
                    == value.expected,
                "Regression: " + std::string(value.cellId) + "/" + std::string(value.displayName));

        std::uint32_t mask = 0;
        for (std::size_t i = 0; i < MapMarkers::categoryCount; ++i)
        {
            const auto category = static_cast<Category>(i);
            const auto& definition = MapMarkers::definition(category);
            require(MapMarkers::isValid(category) && !definition.id.empty() && !definition.label.empty(),
                "Invalid category metadata");
            require(definition.texturePath == "textures/mapmarkers/" + std::string(definition.id) + ".png",
                "Unexpected texture path");
            for (float channel : definition.color)
                require(channel >= 0.f && channel <= 1.f, "Invalid category color");
            require((mask & MapMarkers::bit(category)) == 0, "Repeated category bit");
            mask |= MapMarkers::bit(category);
        }
        require(MapMarkers::categoryCount == 19 && mask == 0x7ffffu, "Changed category mask");
        for (unsigned value : { 19u, 31u, 32u, 127u, 255u })
        {
            const auto category = static_cast<Category>(value);
            require(!MapMarkers::isValid(category) && MapMarkers::bit(category) == 0
                    && !MapMarkers::meaningfulEntrance(category) && MapMarkers::definition(category).id == "misc",
                "Unsafe invalid category handling");
        }
        for (Category category : { Category::City, Category::Town, Category::Settlement, Category::House, Category::Misc })
            require(!MapMarkers::meaningfulEntrance(category), "Ordinary door admitted as a landmark entrance");
        for (Category category : { Category::Cave, Category::Mine, Category::Tomb, Category::Dwemer, Category::Camp })
            require(MapMarkers::meaningfulEntrance(category), "Meaningful entrance omitted");

        std::ifstream input(path);
        require(input.is_open(), "Cannot open factual catalogue: " + path);
        std::string line;
        std::size_t catalogueCount = 0;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line.front() == '#')
                continue;
            const auto tab = line.find('\t');
            require(tab != std::string::npos, "Invalid catalogue row");
            const std::string_view name(line.data(), tab);
            const std::string_view expected(line.data() + tab + 1, line.size() - tab - 1);
            for (bool place : { false, true })
            {
                const auto actual = MapMarkers::classify(place ? "" : name, name, "", place);
                require(MapMarkers::definition(actual).id == expected, "Catalogue mismatch: " + std::string(name));
            }
            ++catalogueCount;
        }
        require(catalogueCount == 2690, "Unexpected factual catalogue size");

        require(MapMarkers::classify(std::string(512, 'q') + " Cave") == Category::Cave, "Long name was truncated");
        require(MapMarkers::classify(std::string(512, 'q')) == Category::Misc, "Long generic name misclassified");
        require(MapMarkers::classify(std::string(256, ' ') + "Balmora  ") == Category::City, "Long whitespace failed");
        require(MapMarkers::classify(std::string(251, 'q') + " Cave") == Category::Cave, "256-byte boundary failed");
        require(MapMarkers::classify(std::string(252, 'q') + " Cave") == Category::Cave, "257-byte boundary failed");
        require(MapMarkers::classify(std::string("Unknown\0Cave", 12)) == Category::Cave, "Embedded null truncated input");

        // Setup, validation, I/O and timer calls lie outside the allocation count.
        // Only normal-length classify() calls are measured, in a mixed-case order.
        std::uint64_t checksum = 0;
        sAllocations = 0;
        const auto started = std::chrono::steady_clock::now();
        sCountAllocations = true;
        for (std::size_t i = 0; i < iterations; ++i)
        {
            const auto& value = MapMarkersTest::cases[i % MapMarkersTest::cases.size()];
            checksum += static_cast<unsigned>(
                MapMarkers::classify(value.cellId, value.displayName, value.doorModel, value.placeLabel));
        }
        sCountAllocations = false;
        const auto stopped = std::chrono::steady_clock::now();
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(stopped - started).count();
        require(sAllocations == 0, "Routine discovery classification allocated heap memory");

        const auto classifierAllocations = sAllocations;
        MapMarkers::SpatialIndex<int> smallIndex;
        require(smallIndex.empty(), "New spatial index is not empty");
        require(smallIndex.insert(-2, -1, 2) && smallIndex.insert(-2, -1, 1), "Spatial insert failed");
        require(!smallIndex.insert(-2, -1, 1), "Duplicate spatial key was inserted");
        smallIndex.insert(-1, -2, 4);
        smallIndex.insert(-1, 0, 3);
        smallIndex.insert(0, 1, 5);
        smallIndex.insert(1, 0, 6);
        std::vector<int> actual;
        auto stats = smallIndex.query(-2, -1, 0, 1, [&](int key) { actual.push_back(key); });
        require(actual == std::vector<int>{ 1, 2, 3, 5 } && stats.xBuckets == 3
                && stats.yBuckets == 3 && stats.keys == 4, "Spatial negative/inclusive bounds failed");
        require(!smallIndex.erase(0, -1, 7) && !smallIndex.erase(-2, 1, 7)
                && !smallIndex.erase(-2, -1, 7), "Absent spatial key reported erased");
        require(smallIndex.erase(-2, -1, 1), "Spatial erase failed");
        require(smallIndex.query(-2, -1, -2, -1, [](int) {}).keys == 1, "Partial bucket was pruned");
        require(smallIndex.erase(-2, -1, 2), "Final spatial key could not be erased");
        require(smallIndex.query(-2, -1, -2, -1, [](int) {}).xBuckets == 0, "Empty column was not pruned");
        require(smallIndex.query(2, -1, 1, 1, [](int) {}).xBuckets == 0, "Inverted X bounds visited buckets");
        require(smallIndex.query(-2, 1, 1, -1, [](int) {}).xBuckets == 0, "Inverted Y bounds visited buckets");
        smallIndex.clear();
        require(smallIndex.empty(), "Spatial clear failed");
        constexpr int minimum = std::numeric_limits<int>::min();
        constexpr int maximum = std::numeric_limits<int>::max();
        smallIndex.insert(minimum, minimum, 1);
        smallIndex.insert(maximum, maximum, 2);
        require(smallIndex.query(minimum, minimum, maximum, maximum, [](int) {}).keys == 2,
            "Extreme bounds failed");
        require(smallIndex.query(maximum, maximum, maximum, maximum, [](int) {}).keys == 1,
            "Maximum-inclusive boundary failed");

        MapMarkers::SpatialIndex<int> largeIndex;
        for (int x = -500; x < 500; ++x)
            for (int y = -50; y < 50; ++y)
                largeIndex.insert(x, y, (x + 500) * 100 + y + 50);
        actual.clear();
        stats = largeIndex.query(-2, -1, 2, 1, [&](int key) { actual.push_back(key); });
        std::vector<int> expected;
        for (int x = -2; x <= 2; ++x)
            for (int y = -1; y <= 1; ++y)
                expected.push_back((x + 500) * 100 + y + 50);
        require(actual == expected && stats.xBuckets == 5 && stats.yBuckets == 15 && stats.keys == 15,
            "Small viewport visited markers outside its 15 occupied buckets");
        std::uint64_t spatialChecksum = 0;
        sAllocations = 0;
        const auto spatialStarted = std::chrono::steady_clock::now();
        sCountAllocations = true;
        for (std::size_t i = 0; i < iterations; ++i)
        {
            const int x = static_cast<int>(i % 996) - 498;
            const int y = static_cast<int>(i % 98) - 49;
            largeIndex.query(x - 2, y - 1, x + 2, y + 1,
                [&](int key) { spatialChecksum += static_cast<unsigned>(key); });
        }
        sCountAllocations = false;
        const auto spatialStopped = std::chrono::steady_clock::now();
        const auto spatialNanoseconds
            = std::chrono::duration_cast<std::chrono::nanoseconds>(spatialStopped - spatialStarted).count();
        require(sAllocations == 0, "Pure spatial queries allocated heap memory");

        std::cout << "{\"passed\":true,\"regression_cases\":" << MapMarkersTest::cases.size()
                  << ",\"catalogue_names\":" << catalogueCount << ",\"catalogue_context_checks\":" << catalogueCount * 2
                  << ",\"category_definitions\":" << MapMarkers::categoryCount
                  << ",\"long_name_checks\":6,\"benchmark_iterations\":" << iterations
                  << ",\"benchmark_nanoseconds\":" << nanoseconds
                  << ",\"nanoseconds_per_classification\":" << static_cast<double>(nanoseconds) / static_cast<double>(iterations)
                  << ",\"routine_heap_allocations\":" << classifierAllocations << ",\"checksum\":" << checksum
                  << ",\"spatial_index\":{\"markers\":100000,\"small_view_x_buckets\":" << stats.xBuckets
                  << ",\"small_view_y_buckets\":" << stats.yBuckets << ",\"small_view_keys\":" << stats.keys
                  << ",\"query_iterations\":" << iterations << ",\"query_nanoseconds\":" << spatialNanoseconds
                  << ",\"query_heap_allocations\":" << sAllocations << ",\"checksum\":" << spatialChecksum << "}}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        sCountAllocations = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
