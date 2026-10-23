#include <components/compiler/locals.hpp>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

int linear(const Compiler::Locals& locals, char type, std::string_view name)
{
    const auto& values = locals.get(type);
    const auto found = std::find(values.begin(), values.end(), name);
    return found == values.end() ? -1 : static_cast<int>(found - values.begin());
}
void check(const Compiler::Locals& locals)
{
    for (char type : {'s', 'l', 'f'})
    {
        for (const auto& name : locals.get(type))
            assert(locals.searchIndex(type, name) == linear(locals, type, name));
        for (auto name : {"missing", "", "UPPER"})
            assert(locals.searchIndex(type, name) == linear(locals, type, name));
    }
}
int main()
{
    Compiler::Locals locals;
    check(locals);
    for (int i = 0; i < 180; ++i)
    {
        for (char type : {'s', 'l', 'f'})
        {
            locals.declare(type, "VAR" + std::to_string((i * 37) % 113));
            check(locals); // includes threshold, growth, duplicates and non-sorted declarations
        }
    }
    locals.declare('s', "shared"); locals.declare('f', "shared");
    assert(locals.getType("shared") == 's');
    assert(locals.getIndex("shared") == linear(locals, 's', "shared"));
    assert(locals.getType("missing") == ' ' && locals.getIndex("missing") == -1);
    bool threw = false;
    try { locals.searchIndex('?', "x"); } catch (const std::logic_error&) { threw = true; }
    assert(threw);
    auto copied = locals;
    locals.clear(); check(locals); check(copied);
    locals.declare('s', "fresh"); assert(locals.searchIndex('s', "fresh") == 0);
    locals = copied; check(locals);
    auto moved = std::move(copied); check(moved);
    Compiler::Locals assigned; assigned = std::move(moved); check(assigned);
    std::ostringstream actual, expected;
    assigned.write(actual);
    expected << std::as_const(assigned).get('s').size() << ' ' << std::as_const(assigned).get('l').size() << ' ' << std::as_const(assigned).get('f').size() << '\n';
    for (char type : {'s', 'l', 'f'}) for (const auto& name : std::as_const(assigned).get(type)) expected << name << ' ';
    assert(actual.str() == expected.str());
    assigned.clear();
    for (int i=0; i<33; ++i) assigned.declare('l', "again"+std::to_string(i));
    check(assigned);
    std::cout << "PASS: actual Compiler::Locals matches linear lookup across growth, duplicates, types, copies, moves, clear, redeclaration and serialization\n";
    // Informational only: never a timing-dependent pass/fail gate.
    for (int count : {8, 16, 32, 64, 256})
    {
        Compiler::Locals bench;
        for (int i=0;i<count;++i) bench.declare('s', "variable_"+std::to_string(i));
        std::string queries[2] = {"variable_"+std::to_string(count-1), "missing"};
        long long sums[2] = {};
        for (int mode=0;mode<2;++mode)
        {
            const auto begin=std::chrono::steady_clock::now();
            for(int i=0;i<400000;++i)
                sums[mode] += mode ? bench.searchIndex('s', queries[i%2]) : linear(bench,'s',queries[i%2]);
            const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
            std::cout << count << (mode ? " indexed " : " linear ") << us << " us\n";
        }
        assert(sums[0]==sums[1]);
    }
}
