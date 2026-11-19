#include "gl_min.hpp"

#include "log.hpp"

#define GL_DEFINE(ret, name, args) ret(APIENTRY* name##_)args = nullptr;
GL_FUNC_LIST(GL_DEFINE)
#undef GL_DEFINE

bool GLLoad(void* (*getProc)(const char*))
{
    bool ok = true;
#define FG_LOADFN(ret, name, args) \
    name##_ = (ret(APIENTRY*)args)getProc(#name); \
    if (!name##_) \
    { \
        Log("gl: missing %s", #name); \
        ok = false; \
    }
    GL_FUNC_LIST(FG_LOADFN)
#undef FG_LOADFN

    return ok;
}
