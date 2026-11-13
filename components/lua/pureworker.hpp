#ifndef OPENMW_COMPONENTS_LUA_PUREWORKER_H
#define OPENMW_COMPONENTS_LUA_PUREWORKER_H

#include <atomic>
#include <cstddef>
#include <string_view>
#include <vector>

namespace LuaUtil
{
    constexpr std::size_t maxWorkerValues = 16384;
    constexpr std::size_t maxWorkerSource = 32768;

    // The text chunk returns a function mapping a dense number array to another one.
    std::vector<double> runPureWorker(std::string_view source, const std::vector<double>& input,
        const std::atomic_bool& cancelled);
}
#endif
