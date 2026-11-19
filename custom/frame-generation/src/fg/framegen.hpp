#pragma once

#include "../gl_min.hpp"

namespace fg
{
    // Platform-neutral swap handler, called from the platform swap hook on the
    // game's render thread after plat's swap target has been recorded.
    // Returns true if the present was swallowed (the FG presenter owns the
    // real backbuffer) — the caller must then skip the real swap.
    bool OnSwapShared();

    // Game thread, before the game destroys its GL context/window: stops the
    // presenter and releases every GL object while the context still exists.
    // FG stays off for the rest of the process.
    void ShutdownIfActive();

#ifdef _WIN32
    // Called from the SwapBuffers hooks on the game's render thread.
    // Owns the decision to passthrough-present or submit to the FG presenter.
    BOOL OnSwap(HDC hdc, BOOL(WINAPI* realSwap)(HDC));
#endif

    // Redirects game binds of framebuffer 0 to the offscreen replacement FBO.
    void RemapBindFramebuffer(GLenum target, GLuint* framebuffer);
    // Maps GL_BACK/GL_FRONT draw/read buffer selection onto the replacement
    // FBO's color attachment while redirect is active.
    void RemapDrawBuffer(GLenum* buf);
    void RemapReadBuffer(GLenum* buf);
    // Notes that the game read pixels from the redirected default framebuffer.
    // OpenMW does this during its auxiliary save/screenshot render traversal;
    // the following swap is suppressed so that GUI-less helper frame is never
    // presented to the player.
    void NoteFramebufferReadback();
    // Makes glGetIntegerv(GL_*_FRAMEBUFFER_BINDING) report 0 for the
    // replacement FBO so the engine's state cache stays coherent.
    void SpoofFramebufferBinding(GLint* result);
}
