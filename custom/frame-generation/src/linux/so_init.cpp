// LD_PRELOAD entry point. No MinHook on Linux — interposition is static: the
// dynamic linker binds the game's calls to our exported symbols because the
// preloaded .so precedes libSDL2/libGL in the global search order. The
// constructor only decides whether this process is the game and where the
// log/ini live (next to the .so, falling back to /tmp).
#include "../config.hpp"
#include "../log.hpp"
#include "linux_shared.hpp"

#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace
{
    bool g_armed = false;
}

namespace lin
{
    bool armed()
    {
        return g_armed;
    }
}

__attribute__((constructor)) static void fgInit()
{
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0)
        return;
    exe[n] = 0;
    const char* base = strrchr(exe, '/');
    base = base ? base + 1 : exe;
    // Only arm inside the game itself; launcher/wizard runs stay passthrough.
    // Distro packages name the binary "openmw", the official openmw.org
    // tarball ships "openmw.x86_64".
    if (strcmp(base, "openmw") != 0 && strcmp(base, "openmw.x86_64") != 0)
        return;

    // log + ini live next to the .so (wherever the user put it)
    char dir[PATH_MAX] = ".";
    Dl_info info{};
    if (dladdr((void*)&fgInit, &info) && info.dli_fname)
    {
        snprintf(dir, sizeof(dir), "%s", info.dli_fname);
        char* slash = strrchr(dir, '/');
        if (slash)
            *slash = 0;
    }
    if (access(dir, W_OK) != 0)
        snprintf(dir, sizeof(dir), "/tmp");

    char path[PATH_MAX + 32];
    snprintf(path, sizeof(path), "%s/openmw_fg.log", dir);
    LogInit(path);
    Log("openmw-fg: init (pid %d), linux/GLX build", (int)getpid());

    snprintf(path, sizeof(path), "%s/openmw_fg.ini", dir);
    ConfigLoad(path);

    lin::resolveRealGl();
    g_armed = true;
    Log("hooks: interposers armed (SDL_GL_SwapWindow, SDL_PollEvent, glXGetProcAddress, glGetIntegerv, glDrawBuffer, glReadBuffer)");
}
