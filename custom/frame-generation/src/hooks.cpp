#include "hooks.hpp"

#include "fg/framegen.hpp"
#include "fg/overlay.hpp"
#include "log.hpp"

#include <MinHook.h>

#include <cstring>

BOOL(WINAPI* real_SwapBuffers)(HDC) = nullptr;
BOOL(WINAPI* real_wglSwapBuffers)(HDC) = nullptr;
PROC(WINAPI* real_wglGetProcAddress)(LPCSTR) = nullptr;
void(APIENTRY* real_glGetIntegerv)(GLenum, GLint*) = nullptr;
void(APIENTRY* real_glDrawBuffer)(GLenum) = nullptr;
void(APIENTRY* real_glReadBuffer)(GLenum) = nullptr;
void(APIENTRY* real_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
void(APIENTRY* real_glBindFramebuffer)(GLenum, GLuint) = nullptr;
void(APIENTRY* real_glBindFramebufferEXT)(GLenum, GLuint) = nullptr;
BOOL(WINAPI* real_SetCursorPos)(int, int) = nullptr;
BOOL(WINAPI* real_ClipCursor)(const RECT*) = nullptr;

namespace
{
    // Reentry guard: gdi32!SwapBuffers and opengl32!wglSwapBuffers can call
    // into each other inside the driver stack; only the outermost hook runs FG.
    thread_local int t_inSwap = 0;

    BOOL WINAPI hookSwapBuffers(HDC hdc)
    {
        if (t_inSwap)
            return real_SwapBuffers(hdc);
        t_inSwap = 1;
        BOOL r = fg::OnSwap(hdc, real_SwapBuffers);
        t_inSwap = 0;
        return r;
    }

    BOOL WINAPI hookWglSwapBuffers(HDC hdc)
    {
        if (t_inSwap)
            return real_wglSwapBuffers(hdc);
        t_inSwap = 1;
        BOOL r = fg::OnSwap(hdc, real_wglSwapBuffers);
        t_inSwap = 0;
        return r;
    }

    void APIENTRY hookGlBindFramebuffer(GLenum target, GLuint framebuffer)
    {
        fg::RemapBindFramebuffer(target, &framebuffer);
        real_glBindFramebuffer(target, framebuffer);
    }

    void APIENTRY hookGlBindFramebufferEXT(GLenum target, GLuint framebuffer)
    {
        fg::RemapBindFramebuffer(target, &framebuffer);
        real_glBindFramebufferEXT(target, framebuffer);
    }

    PROC WINAPI hookWglGetProcAddress(LPCSTR name)
    {
        PROC p = real_wglGetProcAddress(name);
        if (!p || (uintptr_t)p <= 3 || (intptr_t)p == -1)
            return p;

        if (!strcmp(name, "glBindFramebuffer"))
        {
            real_glBindFramebuffer = (void(APIENTRY*)(GLenum, GLuint))p;
            return (PROC)hookGlBindFramebuffer;
        }
        if (!strcmp(name, "glBindFramebufferEXT"))
        {
            real_glBindFramebufferEXT = (void(APIENTRY*)(GLenum, GLuint))p;
            return (PROC)hookGlBindFramebufferEXT;
        }
        return p;
    }

    void APIENTRY hookGlGetIntegerv(GLenum pname, GLint* params)
    {
        real_glGetIntegerv(pname, params);
        if (params
            && (pname == GL_FRAMEBUFFER_BINDING || pname == GL_READ_FRAMEBUFFER_BINDING))
            fg::SpoofFramebufferBinding(params);
    }

    void APIENTRY hookGlDrawBuffer(GLenum buf)
    {
        fg::RemapDrawBuffer(&buf);
        real_glDrawBuffer(buf);
    }

    void APIENTRY hookGlReadBuffer(GLenum buf)
    {
        fg::RemapReadBuffer(&buf);
        real_glReadBuffer(buf);
    }

    void APIENTRY hookGlReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
        GLenum format, GLenum type, void* pixels)
    {
        // OpenMW's save/F12 screenshot path performs an auxiliary framebuffer
        // readback. Mark that traversal so it is not surfaced by the async
        // presenter as a visible UI flash. The readback itself is unchanged.
        fg::NoteFramebufferReadback();
        real_glReadPixels(x, y, width, height, format, type, pixels);
    }

    // While the overlay menu is open the game must not recenter or re-clip the
    // cursor, or the menu would be unusable in relative-mouse mode.
    BOOL WINAPI hookSetCursorPos(int x, int y)
    {
        if (fg::overlay::MenuOpen())
            return TRUE;
        return real_SetCursorPos(x, y);
    }

    BOOL WINAPI hookClipCursor(const RECT* rect)
    {
        if (fg::overlay::MenuOpen())
        {
            fg::overlay::NoteClipRequest(rect);
            return TRUE;
        }
        return real_ClipCursor(rect);
    }

    bool createHook(const wchar_t* module, const char* name, void* detour, void* originalOut)
    {
        MH_STATUS s = MH_CreateHookApi(module, name, detour, (LPVOID*)originalOut);
        if (s != MH_OK)
        {
            Log("hooks: MH_CreateHookApi(%ls!%s) failed: %s", module, name, MH_StatusToString(s));
            return false;
        }
        return true;
    }
}

bool Hooks_Install()
{
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK)
    {
        Log("hooks: MH_Initialize failed: %s", MH_StatusToString(s));
        return false;
    }

    bool ok = true;
    ok &= createHook(L"gdi32.dll", "SwapBuffers", (void*)hookSwapBuffers, &real_SwapBuffers);
    ok &= createHook(L"opengl32.dll", "wglSwapBuffers", (void*)hookWglSwapBuffers, &real_wglSwapBuffers);
    ok &= createHook(L"opengl32.dll", "wglGetProcAddress", (void*)hookWglGetProcAddress, &real_wglGetProcAddress);
    ok &= createHook(L"opengl32.dll", "glGetIntegerv", (void*)hookGlGetIntegerv, &real_glGetIntegerv);
    ok &= createHook(L"opengl32.dll", "glDrawBuffer", (void*)hookGlDrawBuffer, &real_glDrawBuffer);
    ok &= createHook(L"opengl32.dll", "glReadBuffer", (void*)hookGlReadBuffer, &real_glReadBuffer);
    ok &= createHook(L"opengl32.dll", "glReadPixels", (void*)hookGlReadPixels, &real_glReadPixels);
    ok &= createHook(L"user32.dll", "SetCursorPos", (void*)hookSetCursorPos, &real_SetCursorPos);
    ok &= createHook(L"user32.dll", "ClipCursor", (void*)hookClipCursor, &real_ClipCursor);

    if (!ok)
        return false;

    s = MH_EnableHook(MH_ALL_HOOKS);
    if (s != MH_OK)
    {
        Log("hooks: MH_EnableHook failed: %s", MH_StatusToString(s));
        return false;
    }

    Log("hooks: installed (OpenGL/framegen + original F8 cursor handling + readback suppression)");
    return true;
}
