#pragma once

/*
    Draws CPU culled sprites straight into the host's (GD's) OpenGL context.

    Used for OpenGL (in-context) mode with CPU culling instead of bgfx: a bgfx
    frame resets and re-specifies a lot of GL state, and Bismuth needs one per
    composite group. Where every GL call is expensive (Wine + Box64 on ARM,
    drivers running GL on a separate thread), that dominated the frame time.
    This draws a group with about a dozen state queries and a few calls per
    layer, and only restores the state it touched.

    Vertices are those of CpuCuller: { x, y, u, v, rgba8 }, already transformed
    and colored, in the object layer's space (multiplied by the MVP here).
*/

#include <core/Types.hpp>
#include "HostGL.hpp"

namespace bismuth::gfx {

class HostSpriteRenderer {
public:
    struct Layer {
        u32 firstVertex = 0;
        u32 vertexCount = 0;
        GLuint texture = 0;
        bool additive = false; // SRC_ALPHA, ONE (as cocos2d); otherwise ONE, ONE_MINUS_SRC_ALPHA
    };

    /// GD's context must be current. `allowVertexArray` = false forces the
    /// path without an own VAO (GLES 2 without OES_vertex_array_object; tests).
    bool init(const HostGLInfo& info, bool allowVertexArray = true);

    /// `contextLost`: the GL objects died with the context, don't delete them.
    void destroy(bool contextLost);

    bool isInitialized() const { return program != 0; }

    /// Vertices for the next draw() calls (uploaded once, at the first draw).
    /// The memory must stay valid until then.
    void setVertices(const void* vertices, u32 vertexCount);

    /// Draws `count` layers in order into the current framebuffer, with the
    /// current viewport. Restores every GL state it changes.
    void draw(const float mvp[16], const Layer* layers, u32 count);

private:
    void bindAttributes();

    const HostGLInfo* host = nullptr;
    GLuint program = 0;
    GLint mvpLocation = -1;
    GLint textureLocation = -1;
    GLuint vertexBuffer = 0;
    GLuint vertexArray = 0; // own VAO, so GD's vertex attribute state is never touched

    const void* pendingVertices = nullptr;
    u32 pendingVertexCount = 0;
    bool uploadPending = false;
};

} // namespace bismuth::gfx
