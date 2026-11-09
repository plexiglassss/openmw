#ifndef MWLUA_CELLBINDINGS_H
#define MWLUA_CELLBINDINGS_H

#include "object.hpp"

#include <cstdint>
#include <optional>

namespace MWLua
{
    struct Context;

    void initCellBindingsForLocalScripts(const Context& context);
    void initCellBindingsForGlobalScripts(const Context& context);

    // Native implementation shared by the Lua binding and its mutation/parity
    // tests. A null result denotes an unsupported type, not an empty cell.
    ObjectIdList queryCellObjects(MWWorld::CellStore& cell, std::optional<std::uint32_t> type, bool useCache = true);
}

#endif // MWLUA_CELLBINDINGS_H
