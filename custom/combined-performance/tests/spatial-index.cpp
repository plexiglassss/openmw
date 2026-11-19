// Standalone production-header regression and construction-inclusive benchmark.
// Built with GCC/ASan locally and MSVC in the Windows workflow. No game fixtures.
#include <components/misc/spatialindex.hpp>
#include <bit>
#include <chrono>
#include <iostream>
#include <list>
#include <random>
#include <stdexcept>
#include <string>

using Index = Misc::SpatialIndex;
using P = Index::Position;
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
std::vector<P> spread(std::size_t count)
{
    std::vector<P> p;
    std::mt19937 rng(314159);
    std::uniform_real_distribution<float> d(-32768, 32768);
    for (std::size_t i = 0; i < count; ++i) p.push_back({ d(rng), d(rng) });
    return p;
}
void parity(const std::vector<P>& p, P origin, float radius)
{
    Index index(p);
    for (int mode = 0; mode < 2; ++mode)
    {
        const float square = radius * radius;
        const double broadRadius = mode ? std::sqrt(double(square)) : radius;
        const auto candidates = index.query(origin, broadRadius);
        if (!candidates) continue;
        require(std::is_sorted(candidates->begin(), candidates->end()), "order changed");
        require(std::adjacent_find(candidates->begin(), candidates->end()) == candidates->end(), "duplicate index");
        for (std::size_t i = 0; i < p.size(); ++i)
        {
            float x = p[i][0] - origin[0], y = p[i][1] - origin[1];
            float squareDistance = x*x + y*y;
            bool accepted = mode ? squareDistance <= square : std::sqrt(squareDistance) <= radius;
            require(!accepted || std::binary_search(candidates->begin(), candidates->end(), i), "lost exact match");
        }
    }
}
void checkNumeric()
{
    auto p = spread(512);
    p[0] = { -1024, 0 }; p[1] = p[0];
    parity(p, { 0.001f, 0 }, 1024.0009765625f);
    p[2] = { 1e-23f, 0 }; p[3] = { 0, 0 };
    parity(p, { 0, 0 }, 0);
    parity(p, { 0, 0 }, 1e-30f);
    parity(p, { 0, 0 }, -1);
    parity(p, { 0, 0 }, std::numeric_limits<float>::max());
    std::mt19937 rng(12);
    std::uniform_real_distribution<float> d(-40000,40000), radius(0,20000);
    for (int i=0; i<2000; ++i) parity(p, {d(rng),d(rng)}, radius(rng));
    // Wide-exponent floats exercise cancellation, subnormals and overflow.
    for (int batch=0; batch<200; ++batch)
    {
        for (auto& q : p)
            for (auto& v : q) v = std::bit_cast<float>(std::uint32_t(rng()) & 0xfeffffffu);
        P origin = p[batch];
        float r = std::abs(p[(batch+1)%p.size()][0]);
        parity(p, origin, r);
    }
}
void checkFallbacks()
{
    auto p=spread(128);
    Index index(p);
    require(!index.query({0,0},-1), "negative radius must fall back");
    require(!index.query({0,0},INFINITY), "unbounded query must fall back");
    require(!index.query({NAN,0},10), "nonfinite centre must fall back");
    require(!index.query({0,0},1e10), "dense query must fall back");
    p[12][1]=NAN;
    require(!Index(p).query({0,0},1), "nonfinite input must fall back");
    require(!Misc::spatialQueryIsSelective(p.size(), [&](auto){return P{0,0};}, {0,0}, 10), "dense build heuristic");
    require(Misc::spatialQueryIsSelective(p.size(), [&](auto i){return P{float(i*100),0};}, {0,0}, 1), "sparse build heuristic");
}
void checkMutation()
{
    using It=std::list<int>::const_iterator;
    std::list<int> live{0,1,2,3,4};
    std::vector<It> candidates{std::next(live.cbegin()),std::next(live.cbegin(),3)};
    std::vector<int> seen;
    bool changed=false;
    Misc::visitSpatialCandidates([&]{return live.cend();}, std::span<const It>(candidates), [&]{return changed;}, [&](int n){
        seen.push_back(n);
        if(n==1){ *std::next(live.begin(),2)=22; live.push_back(5); changed=true; }
        return true;
    });
    require(seen==std::vector<int>({1,22,3,4,5}), "mutation must resume remaining live list once");
    seen.clear();
    // Recursive query owns its own selection; the outer selection remains valid.
    candidates={live.cbegin(),std::next(live.cbegin(),3)};
    Misc::visitSpatialCandidates([&]{return live.cend();},std::span<const It>(candidates),[]{return false;},[&](int n){
        seen.push_back(n);
        if(n==0){ std::vector<It> inner{std::next(live.cbegin())};
            Misc::visitSpatialCandidates([&]{return live.cend();},std::span<const It>(inner),[]{return false;},[&](int x){seen.push_back(x);return true;}); }
        return true;
    });
    require(seen==std::vector<int>({0,1,3}),"reentrant traversal");
    std::size_t calls=0, storage=0;
    const auto original=storage;
    Misc::visitSpatialCandidates([&]{return live.cend();},std::span<const It>(candidates),[]{return true;},[&](int){
        ++calls; live.clear(); ++storage; return storage==original;
    });
    require(calls==1,"clear must stop before accessing invalid iterators");
}
struct Sample { std::size_t id; float distance; };
std::vector<Sample> query(const std::vector<P>& p, P origin, float radius, const Index* index)
{
    const auto candidates=index?index->query(origin,radius):std::nullopt;
    std::vector<Sample> result;
    result.reserve(candidates?candidates->size():p.size());
    const auto measure=[&](std::size_t i){
        const float x=origin[0]-p[i][0], y=origin[1]-p[i][1];
        const float distance=std::sqrt(x*x+y*y);
        if(distance<=radius)result.push_back({i,distance});
    };
    if(candidates)for(auto i:*candidates)measure(i);
    else for(std::size_t i=0;i<p.size();++i)measure(i);
    return result;
}
void benchmark()
{
    const auto p=spread(4096);
    std::size_t checksum=0;
    bool first=true;
    std::cout<<"[";
    for(bool dense:{false,true})for(int queries:{1,4,16,64})
    {
        double micros[2]{};
        for(int indexed=0;indexed<2;++indexed)
        {
            auto start=std::chrono::steady_clock::now();
            for(int repeat=0;repeat<80;++repeat)
            {
                std::optional<Index> index;
                for(int q=0;q<queries;++q)
                {
                    P origin{float(q*7),float(q*3)};
                    float radius=dense?100000.f:500.f;
                    if(indexed && !index && q>=7 && Misc::spatialQueryIsSelective(p.size(),[&](auto i){return p[i];},origin,radius))index.emplace(p);
                    const auto result=query(p,origin,radius,index?&*index:nullptr);
                    checksum+=result.size();
                }
            }
            micros[indexed]=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/80;
        }
        if(!first)std::cout<<",";
        first=false;
        std::cout<<"{\"dense\":"<<(dense?"true":"false")<<",\"queries_per_rebuilt_source\":"<<queries<<",\"linear_us\":"<<micros[0]<<",\"indexed_us\":"<<micros[1]<<"}";
    }
    std::cout<<"],\"checksum\":"<<checksum;
}
int main()
{
    try
    {
        checkNumeric();checkFallbacks();checkMutation();
        std::cout<<"{\"result\":\"PASS\",\"checks\":[\"float_boundary\",\"underflow\",\"random_parity\",\"extreme_float_parity\",\"fallbacks\",\"mutation_order\",\"reentrancy\",\"storage_clear\"],\"benchmarks\":";
        benchmark();std::cout<<",\"gameplay_fps_measured\":false}\n";
    }
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
