#pragma once

#include "../gl_min.hpp"

namespace fg
{
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
