#pragma once

#include <core/Types.hpp>
#include <bgfx/bgfx.h>
#include "HostGL.hpp"
#include <vector>

namespace bismuth::gfx {

/*
    Makes Geometry Dash's (cocos2d's) GL textures, e.g. the sprite sheets, usable by bgfx.

    - OpenGL host mode: the bgfx texture is pointed at the existing GL texture
      (bgfx::overrideInternal), no copy.
    - Vulkan / Metal: the texture is read back once through an FBO and uploaded.
*/
class HostTextures {
public:
    ~HostTextures();

    /// Registers a texture; returns its bgfx handle (invalid on failure).
    bgfx::TextureHandle import(GLuint glTexture, u32 width, u32 height);

    /// Must be called once after the bgfx frame that followed `import` calls
    /// (host mode needs bgfx to create its placeholder textures first).
    void finishImports();

    void destroyAll();

private:
    struct Entry {
        GLuint glTexture = 0;
        u32 width = 0;
        u32 height = 0;
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        bool pendingOverride = false;
    };

    std::vector<Entry> entries;
};

/// Reads an RGBA8 copy of a GL texture (via a temporary FBO). Saves/restores GL state.
bool readHostTexture(const HostGLInfo& host, GLuint texture, u32 width, u32 height, std::vector<u8>& out);

} // namespace bismuth::gfx
