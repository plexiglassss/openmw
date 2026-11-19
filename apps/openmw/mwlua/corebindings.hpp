#ifndef MWLUA_COREBINDINGS_H
#define MWLUA_COREBINDINGS_H

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    void addIsolatedWorkerBindings(sol::table& api, const Context& context);
    void addCoreTimeBindings(sol::table& api, const Context& context);

    sol::table initCorePackage(const Context& context);
}

#endif // MWLUA_COREBINDINGS_H
