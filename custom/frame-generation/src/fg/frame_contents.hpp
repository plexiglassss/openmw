#pragma once

#include "../gl_min.hpp"

namespace fg
{
    // Seed the first offscreen target from the real, just-rendered back buffer.
    // A loading traversal may copy/read its background before drawing anything;
    // the texture ring must already contain the native image when redirection starts.
    inline void SeedFrameContents(GLuint sourceFramebuffer, GLenum sourceBuffer,
        GLuint destinationFramebuffer, int width, int height)
    {
        GLint oldRead = 0, oldDraw = 0, sourceReadBuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldDraw);
        const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        constexpr GLenum framebufferSrgb = 0x8DB9;
        const GLboolean srgb = glIsEnabled(framebufferSrgb);
        glBindFramebuffer_(GL_READ_FRAMEBUFFER, sourceFramebuffer);
        glGetIntegerv(GL_READ_BUFFER, &sourceReadBuffer);
        glReadBuffer(sourceBuffer);
        glBindFramebuffer_(GL_DRAW_FRAMEBUFFER, destinationFramebuffer);
        glDisable(GL_SCISSOR_TEST);
        glDisable(framebufferSrgb);
        glBlitFramebuffer_(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glReadBuffer(static_cast<GLenum>(sourceReadBuffer));
        glBindFramebuffer_(GL_READ_FRAMEBUFFER, static_cast<GLuint>(oldRead));
        glBindFramebuffer_(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(oldDraw));
        if (scissor) glEnable(GL_SCISSOR_TEST);
        if (srgb) glEnable(framebufferSrgb);
    }

    // The newly writable slot may contain a much older viewpoint. OpenMW's
    // loading camera copies its contents BEFORE drawing, so retain the latest
    // native image rather than exposing that slot's historical contents.
    // Call on the game context, after the destination's reuse fence wait and
    // before attaching it for the next traversal. Source is read-only here.
    // Image copy preserves all GL bindings, masks, scissor and sRGB state.
    inline void PreserveFrameContents(GLuint source, GLuint destination, int width, int height)
    {
        glCopyImageSubData_(source, GL_TEXTURE_2D, 0, 0, 0, 0,
            destination, GL_TEXTURE_2D, 0, 0, 0, 0, width, height, 1);
    }
}
