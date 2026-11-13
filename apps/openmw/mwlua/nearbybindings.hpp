#ifndef MWLUA_NEARBYBINDINGS_H
#define MWLUA_NEARBYBINDINGS_H

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    void addNearbyDistanceBindings(sol::table& api, const Context& context);
    sol::table initNearbyPackage(const Context& context);
}

#endif // MWLUA_NEARBYBINDINGS_H
