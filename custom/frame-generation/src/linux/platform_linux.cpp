// GLX/SDL implementation of the plat:: abstraction. X11-only: on Wayland
// sessions OpenMW must run through XWayland (SDL_VIDEODRIVER=x11); a pure
// EGL/Wayland context is detected and reported as a permanent init failure.
#include "../platform.hpp"

#include "../config.hpp"
#include "../gl_min.hpp"
#include "../hooks.hpp"
#include "../log.hpp"
#include "linux_shared.hpp"

#include <GL/glx.h>
#include <X11/Xlib.h>

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace
{
    Display* g_dpy = nullptr;
    GLXDrawable g_drawable = 0;
    GLXContext g_gameCtx = nullptr;
    GLXContext g_fgCtx = nullptr;
    SDL_Window* g_gameWin = nullptr;
    int g_screen = 0;

    // Swallow X errors around glXCreateContextAttribsARB probing (a rejected
    // version/profile raises BadMatch, which would kill the process).
    int ignoreXError(Display*, XErrorEvent*)
    {
        return 0;
    }

    // glXGetProcAddress returns non-null dispatch stubs even for absent
    // extensions (calling one crashes), so gate every glX extension function
    // on the server/client extension string.
    bool hasGlxExtension(const char* name)
    {
        if (!g_dpy)
            return false;
        const char* exts = glXQueryExtensionsString(g_dpy, g_screen);
        if (!exts)
            return false;
        size_t len = strlen(name);
        for (const char* p = exts; (p = strstr(p, name)) != nullptr; p += len)
            if ((p == exts || p[-1] == ' ') && (p[len] == ' ' || p[len] == 0))
                return true;
        return false;
    }
}

namespace plat
{
    uint64_t nowNs()
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    }

    void highResTimerBegin() {}
    void highResTimerEnd() {}

    void sleepMs(double ms)
    {
        if (ms <= 0)
            return;
        timespec ts{};
        ts.tv_sec = (time_t)(ms / 1000.0);
        ts.tv_nsec = (long)((ms - ts.tv_sec * 1000.0) * 1e6);
        nanosleep(&ts, nullptr);
    }

    void cpuRelax()
    {
        __builtin_ia32_pause();
    }

    int captureGameContext()
    {
        g_gameCtx = glXGetCurrentContext();
        g_dpy = glXGetCurrentDisplay();
        g_drawable = glXGetCurrentDrawable();
        g_gameWin = lin::swapWindow();
        if (g_gameCtx && g_dpy && g_drawable && g_gameWin)
            return 1;

        static int attempts = 0;
        if (++attempts >= 300)
        {
            Log("fg: no GLX context after %d swaps — EGL/Wayland video driver? "
                "Run OpenMW with SDL_VIDEODRIVER=x11 (XWayland). Frame generation unavailable.",
                attempts);
            return -1;
        }
        return 0;
    }

    void forgetGameContext()
    {
        g_gameCtx = nullptr;
        g_dpy = nullptr;
        g_drawable = 0;
        g_gameWin = nullptr;
    }

    bool isGameContextCurrent()
    {
        return g_gameCtx && glXGetCurrentContext() == g_gameCtx;
    }

    bool isGameSwapTarget()
    {
        return g_gameWin && lin::swapWindow() == g_gameWin && glXGetCurrentContext() == g_gameCtx;
    }

    bool getDrawableSize(int& w, int& h)
    {
        // game thread only (SDL is not thread-safe)
        static auto realGetSize = (void (*)(SDL_Window*, int*, int*))lin::realSdl("SDL_GL_GetDrawableSize");
        if (!realGetSize || !g_gameWin)
            return false;
        realGetSize(g_gameWin, &w, &h);
        return true;
    }

    bool createPresenterContext()
    {
        int fbid = 0, nelem = 0;
        if (glXQueryContext(g_dpy, g_gameCtx, GLX_SCREEN, &g_screen) != Success
            || glXQueryContext(g_dpy, g_gameCtx, GLX_FBCONFIG_ID, &fbid) != Success)
        {
            Log("plat: glXQueryContext failed");
            return false;
        }
        const int chooseAttribs[] = { GLX_FBCONFIG_ID, fbid, None };
        GLXFBConfig* cfgs = glXChooseFBConfig(g_dpy, g_screen, chooseAttribs, &nelem);
        if (!cfgs || !nelem)
        {
            Log("plat: glXChooseFBConfig found no config for id 0x%X", fbid);
            return false;
        }
        GLXFBConfig cfg = cfgs[0];

        using CreateCtxFn = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
        auto createCtx = hasGlxExtension("GLX_ARB_create_context")
            ? (CreateCtxFn)getProcAddress("glXCreateContextAttribsARB")
            : nullptr;

        XSync(g_dpy, False);
        XErrorHandler old = XSetErrorHandler(ignoreXError);
        if (createCtx)
        {
            const int attribsCore[] = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 3,
                GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None
            };
            g_fgCtx = createCtx(g_dpy, cfg, g_gameCtx, True, attribsCore);
            if (!g_fgCtx)
            {
                const int attribsCompat[] = {
                    GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 3,
                    GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB, None
                };
                g_fgCtx = createCtx(g_dpy, cfg, g_gameCtx, True, attribsCompat);
            }
        }
        if (!g_fgCtx)
            g_fgCtx = glXCreateNewContext(g_dpy, cfg, GLX_RGBA_TYPE, g_gameCtx, True);
        XSync(g_dpy, False);
        XSetErrorHandler(old);
        XFree(cfgs);

        if (!g_fgCtx)
        {
            Log("plat: presenter GLX context creation failed");
            return false;
        }
        return true;
    }

    void destroyPresenterContext()
    {
        if (g_fgCtx)
        {
            glXDestroyContext(g_dpy, g_fgCtx);
            g_fgCtx = nullptr;
        }
    }

    bool presenterMakeCurrent()
    {
        if (!glXMakeCurrent(g_dpy, g_drawable, g_fgCtx))
        {
            Log("plat: presenter glXMakeCurrent failed");
            return false;
        }
        return true;
    }

    void presenterDoneCurrent()
    {
        glXMakeCurrent(g_dpy, None, nullptr);
    }

    void presenterSwapBuffers()
    {
        glXSwapBuffers(g_dpy, g_drawable);
    }

    bool presenterSetSwapInterval(int interval)
    {
        using ExtFn = void (*)(Display*, GLXDrawable, int);
        using MesaFn = int (*)(unsigned int);
        static auto ext = hasGlxExtension("GLX_EXT_swap_control")
            ? (ExtFn)getProcAddress("glXSwapIntervalEXT")
            : nullptr;
        static auto mesa = hasGlxExtension("GLX_MESA_swap_control")
            ? (MesaFn)getProcAddress("glXSwapIntervalMESA")
            : nullptr;
        if (ext)
        {
            ext(g_dpy, g_drawable, interval);
            return true;
        }
        if (mesa && interval >= 0)
        {
            mesa((unsigned int)interval);
            return true;
        }
        return false;
    }

    void* getProcAddress(const char* name)
    {
        using GetProcFn = void* (*)(const unsigned char*);
        static auto real = (GetProcFn)lin::realSym("glXGetProcAddressARB");
        void* p = real ? real((const unsigned char*)name) : nullptr;
        if (!p && lin::glHandle())
            p = dlsym(lin::glHandle(), name);
        return p;
    }

    int displayRefreshHz()
    {
        // game thread only: shares the game's Display connection
        if (!g_dpy)
            return 0;
        void* xrandr = dlopen("libXrandr.so.2", RTLD_LAZY | RTLD_LOCAL);
        if (!xrandr)
            xrandr = dlopen("libXrandr.so", RTLD_LAZY | RTLD_LOCAL);
        if (!xrandr)
            return 0;
        using GetInfoFn = void* (*)(Display*, Window);
        using RateFn = short (*)(void*);
        using FreeFn = void (*)(void*);
        auto getInfo = (GetInfoFn)dlsym(xrandr, "XRRGetScreenInfo");
        auto rate = (RateFn)dlsym(xrandr, "XRRConfigCurrentRate");
        auto freeInfo = (FreeFn)dlsym(xrandr, "XRRFreeScreenConfigInfo");
        int hz = 0;
        if (getInfo && rate && freeInfo)
        {
            void* info = getInfo(g_dpy, DefaultRootWindow(g_dpy));
            if (info)
            {
                hz = rate(info);
                freeInfo(info);
            }
        }
        dlclose(xrandr);
        return hz > 1 ? hz : 0;
    }

    double gameFramerateLimit()
    {
        char path[1024];
        const char* xdg = getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg)
            snprintf(path, sizeof(path), "%s/openmw/settings.cfg", xdg);
        else
        {
            const char* home = getenv("HOME");
            if (!home || !*home)
                return 0.0;
            snprintf(path, sizeof(path), "%s/.config/openmw/settings.cfg", home);
        }
        FILE* f = fopen(path, "r");
        if (!f)
            return 0.0;
        double v = OpenMWSettingsFramerateLimit(f);
        fclose(f);
        return v;
    }

    bool gameWindowFocused()
    {
        return lin::focused();
    }

    bool keyDown(int code)
    {
        return lin::keyDown(code);
    }
}
