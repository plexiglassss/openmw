#pragma once

#include "gl_min.hpp"

// Trampolines to the unhooked originals, for internal use.
// Windows: MinHook trampolines. Linux: dlsym-resolved libGL entry points.
extern void(APIENTRY* real_glGetIntegerv)(GLenum, GLint*);
extern void(APIENTRY* real_glDrawBuffer)(GLenum);
extern void(APIENTRY* real_glReadBuffer)(GLenum);
#ifdef _WIN32
extern void(APIENTRY* real_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
#endif
// Driver entry points for framebuffer binds (captured when the engine resolves
// them through our wglGetProcAddress / glXGetProcAddress interception).
extern void(APIENTRY* real_glBindFramebuffer)(GLenum, GLuint);
extern void(APIENTRY* real_glBindFramebufferEXT)(GLenum, GLuint);

#ifdef _WIN32
extern BOOL(WINAPI* real_SwapBuffers)(HDC);
extern BOOL(WINAPI* real_wglSwapBuffers)(HDC);
extern PROC(WINAPI* real_wglGetProcAddress)(LPCSTR);
// Cursor control, suppressed while the overlay menu is open.
extern BOOL(WINAPI* real_SetCursorPos)(int, int);
extern BOOL(WINAPI* real_ClipCursor)(const RECT*);

bool Hooks_Install();
#endif
