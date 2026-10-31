#include "HostTextures.hpp"
#include "Device.hpp"

#include <bgfx/platform.h>

namespace bismuth::gfx {

HostTextures::~HostTextures() {
    destroyAll();
}

bool readHostTexture(const HostGLInfo& host, GLuint texture, u32 width, u32 height, std::vector<u8>& out) {
    auto& gl = glf();
    GLStateGuard guard(host);

    GLuint fbo = 0;
    gl.glGenFramebuffers(1, &fbo);
    gl.glBindFramebuffer(gl::FRAMEBUFFER, fbo);
    gl.glFramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, texture, 0);

    bool ok = gl.glCheckFramebufferStatus(gl::FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    if (ok) {
        out.resize((size_t)width * height * 4);
        gl.glPixelStorei(gl::PACK_ALIGNMENT, 1);
        gl.glReadPixels(0, 0, (GLsizei)width, (GLsizei)height, gl::RGBA, gl::UNSIGNED_BYTE, out.data());
        ok = gl.glGetError() == gl::NO_ERROR_;
    }

    gl.glDeleteFramebuffers(1, &fbo);
    return ok;
}

bgfx::TextureHandle HostTextures::import(GLuint glTexture, u32 width, u32 height) {
    for (auto& entry : entries) {
        if (entry.glTexture == glTexture && entry.width == width && entry.height == height)
            return entry.handle;
    }

    auto& device = Device::get();
    const u64 flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    Entry entry;
    entry.glTexture = glTexture;
    entry.width = width;
    entry.height = height;

    if (device.isHostMode()) {
        // Placeholder of the same size/format; replaced by the GL texture after a frame.
        entry.handle = bgfx::createTexture2D((u16)width, (u16)height, false, 1, bgfx::TextureFormat::RGBA8, flags);
        entry.pendingOverride = true;
    } else {
        std::vector<u8> pixels;
        if (!readHostTexture(device.getHost(), glTexture, width, height, pixels)) {
            logf(LogLevel::Error, "Bismuth: couldn't read texture %u (%ux%u)", glTexture, width, height);
            return BGFX_INVALID_HANDLE;
        }
        // GL's texture row 0 is the first row of the image as uploaded by cocos, as
        // is bgfx's, so texture coordinates carry over unchanged.
        entry.handle = bgfx::createTexture2D((u16)width, (u16)height, false, 1, bgfx::TextureFormat::RGBA8, flags,
                                             bgfx::copy(pixels.data(), (u32)pixels.size()));
    }

    entries.push_back(entry);
    return entry.handle;
}

void HostTextures::finishImports() {
    // overrideInternal touches GL immediately (it deletes bgfx's placeholder).
    UPtr<GLStateGuard> guard;
    if (Device::get().isHostMode())
        guard = makeUnique<GLStateGuard>(Device::get().getHost());

    for (auto& entry : entries) {
        if (!entry.pendingOverride)
            continue;
        entry.pendingOverride = false;
        if (0 == bgfx::overrideInternal(entry.handle, (uintptr_t)entry.glTexture))
            logf(LogLevel::Error, "Bismuth: overrideInternal failed for GL texture %u", entry.glTexture);
    }
}

void HostTextures::destroyAll() {
    for (auto& entry : entries) {
        // Overridden textures are flagged as shared, so bgfx won't delete the GL texture.
        if (bgfx::isValid(entry.handle))
            bgfx::destroy(entry.handle);
    }
    entries.clear();
}

} // namespace bismuth::gfx
