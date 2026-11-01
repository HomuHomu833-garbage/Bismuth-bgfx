/*
    End-to-end test of the rendering core on Mesa (llvmpipe GL + lavapipe Vulkan).

    A GLX compatibility context plays the role of Geometry Dash's context, with an
    FBO as its "screen". The same synthetic level is rendered with:

        OpenGL in-context  x  GPU culling / CPU culling
        Vulkan + sharing   x  GPU culling / CPU culling

    and all images must match. Run under xvfb-run.
*/
#include "SyntheticScene.hpp"
#include <gfx/Device.hpp>
#include <gfx/HostTextures.hpp>
#include <renderer/BatchRenderer.hpp>
#include <bgfx/platform.h>

#include <X11/Xlib.h>
#include <GL/glx.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace bismuth {
void logMessage(LogLevel level, const char* message) {
    if (level == LogLevel::Debug && !std::getenv("BISMUTH_TEST_VERBOSE"))
        return;
    std::fprintf(stderr, "%s\n", message);
}
}

using namespace bismuth;
using namespace bismuth::gfx;

static constexpr u32 W = 320;
static constexpr u32 H = 240;

struct HostWindow {
    Display* display = nullptr;
    GLXContext context = nullptr;
    GLXPbuffer pbuffer = 0;
    GLuint fbo = 0;
    GLuint fboTexture = 0;
    GLuint sheet = 0;
};

static bool createHost(HostWindow& host) {
    host.display = XOpenDisplay(nullptr);
    if (!host.display) {
        std::fprintf(stderr, "no X display (run with xvfb-run)\n");
        return false;
    }

    int attributes[] = { GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT,
                         GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, None };
    int count = 0;
    GLXFBConfig* configs = glXChooseFBConfig(host.display, DefaultScreen(host.display), attributes, &count);
    if (!configs || count == 0)
        return false;

    int pbufferAttributes[] = { GLX_PBUFFER_WIDTH, (int)W, GLX_PBUFFER_HEIGHT, (int)H, None };
    host.pbuffer = glXCreatePbuffer(host.display, configs[0], pbufferAttributes);
    // Legacy creation: a compatibility profile context, like Geometry Dash's.
    host.context = glXCreateNewContext(host.display, configs[0], GLX_RGBA_TYPE, nullptr, True);
    XFree(configs);
    if (!host.context || !glXMakeContextCurrent(host.display, host.pbuffer, host.pbuffer, host.context))
        return false;

    if (!loadGLFunctions())
        return false;
    auto& gl = glf();

    // "Screen" of the fake game.
    gl.glGenTextures(1, &host.fboTexture);
    gl.glBindTexture(gl::TEXTURE_2D, host.fboTexture);
    gl.glTexImage2D(gl::TEXTURE_2D, 0, (GLint)gl::RGBA8, W, H, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    gl.glGenFramebuffers(1, &host.fbo);
    gl.glBindFramebuffer(gl::FRAMEBUFFER, host.fbo);
    gl.glFramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, host.fboTexture, 0);

    // Sprite sheet: premultiplied pattern with transparent holes, like cocos2d's textures.
    std::vector<u8> pixels(64 * 64 * 4);
    for (u32 y = 0; y < 64; y++) {
        for (u32 x = 0; x < 64; x++) {
            u8* p = &pixels[(y * 64 + x) * 4];
            u8 a = ((x / 8 + y / 8) % 3 == 0) ? 0 : (u8)(128 + (x * 2));
            p[0] = (u8)((x * 4) * a / 255);
            p[1] = (u8)((y * 4) * a / 255);
            p[2] = (u8)(200 * a / 255);
            p[3] = a;
        }
    }
    gl.glGenTextures(1, &host.sheet);
    gl.glBindTexture(gl::TEXTURE_2D, host.sheet);
    gl.glTexImage2D(gl::TEXTURE_2D, 0, (GLint)gl::RGBA8, 64, 64, 0, gl::RGBA, gl::UNSIGNED_BYTE, pixels.data());
    gl.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl.glBindTexture(gl::TEXTURE_2D, 0);
    return true;
}

static void beginHostFrame(const HostWindow& host) {
    auto& gl = glf();
    gl.glBindFramebuffer(gl::FRAMEBUFFER, host.fbo);
    gl.glViewport(0, 0, W, H);
    gl.glClearColor(0.1f, 0.2f, 0.3f, 1.f);
    gl.glClear(gl::COLOR_BUFFER_BIT);
    // cocos2d's usual state: blending on, premultiplied.
    gl.glEnable(gl::BLEND);
    gl.glBlendFuncSeparate(gl::ONE, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
}

static std::vector<u8> readHost(const HostWindow& host) {
    auto& gl = glf();
    std::vector<u8> pixels(W * H * 4);
    gl.glBindFramebuffer(gl::FRAMEBUFFER, host.fbo);
    gl.glPixelStorei(gl::PACK_ALIGNMENT, 1);
    gl.glReadPixels(0, 0, W, H, gl::RGBA, gl::UNSIGNED_BYTE, pixels.data());
    return pixels;
}

static void savePPM(const std::string& path, const std::vector<u8>& rgba) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", W, H);
    for (int y = H - 1; y >= 0; y--)
        for (u32 x = 0; x < W; x++)
            std::fwrite(&rgba[(y * W + x) * 4], 1, 3, f);
    std::fclose(f);
}

/// Puts unusual values in the GL state cocos2d caches, to detect leaks from bgfx.
static void setDistinctiveHostState(const HostWindow& host) {
    auto& gl = glf();
    gl.glActiveTexture(gl::TEXTURE0 + 1);
    gl.glBindTexture(gl::TEXTURE_2D, host.sheet);
    gl.glActiveTexture(gl::TEXTURE0);
    gl.glBindTexture(gl::TEXTURE_2D, host.fboTexture);
    gl.glBlendFuncSeparate(gl::ONE, gl::ONE, gl::ONE_MINUS_SRC_ALPHA, gl::ONE);
    gl.glEnableVertexAttribArray(2);
    gl.glPixelStorei(gl::UNPACK_ALIGNMENT, 2);
}

static bool sameState(const GLStateSnapshot& a, const GLStateSnapshot& b) {
    bool same = a.program == b.program && a.vao == b.vao && a.arrayBuffer == b.arrayBuffer
        && a.elementArrayBuffer == b.elementArrayBuffer && a.activeTexture == b.activeTexture
        && a.texture2D[0] == b.texture2D[0] && a.texture2D[1] == b.texture2D[1]
        && a.drawFramebuffer == b.drawFramebuffer && a.readFramebuffer == b.readFramebuffer
        && std::memcmp(a.viewport, b.viewport, sizeof(a.viewport)) == 0
        && a.blend == b.blend && a.blendSrcRGB == b.blendSrcRGB && a.blendDstRGB == b.blendDstRGB
        && a.blendSrcAlpha == b.blendSrcAlpha && a.blendDstAlpha == b.blendDstAlpha
        && a.blendEqRGB == b.blendEqRGB && a.depthTest == b.depthTest && a.depthMask == b.depthMask
        && a.cullFace == b.cullFace && a.frontFace == b.frontFace && a.scissorTest == b.scissorTest
        && a.unpackAlignment == b.unpackAlignment && a.enabledAttribs == b.enabledAttribs;
    return same;
}

static LayerBlend layerBlend(u32 layer) {
    return layer == 0 ? LayerBlend::Normal : LayerBlend::Additive;
}

struct Result {
    std::string name;
    bool ok = false;
    std::string details;
    std::vector<u8> image;
};

static Result renderWith(HostWindow& host, const test::Scene& scene, BackendPreference backend, CullingPreference culling, const char* name) {
    Result result;
    result.name = name;

    auto& device = Device::get();
    beginHostFrame(host);

    std::string report;
    if (!device.init({ backend, culling }, report)) {
        result.details = "init failed: " + report;
        return result;
    }
    result.details = device.describe();

    const bool wantGpu = culling == CullingPreference::Gpu;
    if (wantGpu != (device.getCullingPath() == CullingPath::Gpu)) {
        result.details += " [culling path mismatch]";
        device.shutdown();
        return result;
    }

    HostTextures textures;
    auto sheet = textures.import(host.sheet, 64, 64);

    BatchRenderer batch;
    if (!batch.init(scene.staticData, device.getCullingPath(), device.getRendererType())) {
        result.details += " [BatchRenderer init failed]";
        device.shutdown();
        return result;
    }
    batch.setSortedBatch(&scene.sorted);

    SharedTarget target;
    if (!device.isHostMode() && !device.getInterop()->createTarget(target, W, H)) {
        result.details += " [shared target failed]";
        batch.shutdown();
        device.shutdown();
        return result;
    }

    // A few frames, so that the cross-API synchronization runs through its steady state.
    for (int frame = 0; frame < 3; frame++) {
        beginHostFrame(host);
        setDistinctiveHostState(host);
        HostGLInfo info = device.getHost();
        auto before = std::make_unique<GLStateGuard>(info);
        batch.updateFrame(scene.frame, scene.gameState);

        if (device.isHostMode()) {
            if (frame == 0) {
                device.flushHost();
                textures.finishImports();
            }
            // Culling goes out with the first layer's flush (views run in order).
            batch.cull(0);
            for (u32 l = 0; l < scene.layerCount; l++) {
                device.setHostViewRect(1);
                bgfx::setViewFrameBuffer(1, BGFX_INVALID_HANDLE);
                batch.drawLayer(1, l, sheet, layerBlend(l), false);
                device.flushHost();
            }
        } else {
            batch.cull(0);
            bgfx::setViewFrameBuffer(1, target.framebuffer);
            bgfx::setViewRect(1, 0, 0, (u16)W, (u16)H);
            bgfx::setViewClear(1, BGFX_CLEAR_COLOR, 0x00000000);
            bgfx::touch(1);
            for (u32 l = 0; l < scene.layerCount; l++)
                batch.drawLayer(1, l, sheet, layerBlend(l), true);
            device.submitNative();

            auto& gl = glf();
            gl.glBindFramebuffer(gl::FRAMEBUFFER, host.fbo);
            gl.glViewport(0, 0, W, H);
            device.getInterop()->beforeHostRead();
            device.getCompositor().draw(target);
            device.getInterop()->afterHostRead();
        }
        device.endGameFrame();

        GLStateGuard after(info);
        if (!sameState(before->snapshot(), after.snapshot())) {
            const auto& a = before->snapshot();
            const auto& b = after.snapshot();
#define DIFF(F) if (a.F != b.F) std::fprintf(stderr, "  state %s: %d -> %d\n", #F, (int)a.F, (int)b.F);
            DIFF(program) DIFF(vao) DIFF(arrayBuffer) DIFF(elementArrayBuffer) DIFF(activeTexture)
            DIFF(texture2D[0]) DIFF(texture2D[1]) DIFF(drawFramebuffer) DIFF(readFramebuffer)
            DIFF(blendSrcRGB) DIFF(blendDstRGB) DIFF(blendSrcAlpha) DIFF(blendDstAlpha) DIFF(blendEqRGB)
            DIFF(depthTest) DIFF(depthMask) DIFF(cullFace) DIFF(frontFace) DIFF(scissorTest)
            DIFF(unpackAlignment) DIFF(enabledAttribs) DIFF(viewport[2])
#undef DIFF
            result.details += " [host GL state not restored]";
        }
    }

    glf().glFinish();
    result.image = readHost(host);

    if (target.isValid() || bgfx::isValid(target.framebuffer))
        device.getInterop()->destroyTarget(target);
    batch.shutdown();
    textures.destroyAll();
    device.shutdown();

    result.ok = result.details.find("[host GL state") == std::string::npos;
    return result;
}

static bool compareImages(const Result& a, const Result& b, u32 tolerance) {
    u32 maxDiff = 0, differing = 0;
    for (size_t i = 0; i < a.image.size(); i++) {
        u32 d = (u32)std::abs((int)a.image[i] - (int)b.image[i]);
        maxDiff = std::max(maxDiff, d);
        if (d > tolerance) differing++;
    }
    // A handful of pixels may differ at triangle edges between rasterizer paths.
    bool ok = differing <= a.image.size() / 2000;
    std::printf("  %-26s vs %-26s max diff %3u, %6u values > %u  %s\n",
                a.name.c_str(), b.name.c_str(), maxDiff, differing, tolerance, ok ? "OK" : "MISMATCH");
    return ok;
}

int main() {
    HostWindow host;
    if (!createHost(host)) {
        std::fprintf(stderr, "failed to create host GL context\n");
        return 2;
    }

    HostGLInfo info;
    queryHostGL(info);
    std::printf("host GL: %s (%s)\n", info.version.c_str(), info.renderer.c_str());

    auto scene = test::makeScene(W, H, 900, 42);

    std::vector<Result> results;
    results.push_back(renderWith(host, scene, BackendPreference::OpenGL, CullingPreference::Gpu, "OpenGL / GPU culling"));
    results.push_back(renderWith(host, scene, BackendPreference::OpenGL, CullingPreference::Cpu, "OpenGL / CPU culling"));
    results.push_back(renderWith(host, scene, BackendPreference::Native, CullingPreference::Gpu, "Vulkan / GPU culling"));
    results.push_back(renderWith(host, scene, BackendPreference::Native, CullingPreference::Cpu, "Vulkan / CPU culling"));

    bool ok = true;
    for (auto& r : results) {
        std::printf("%-26s %s: %s\n", r.name.c_str(), r.ok ? "rendered" : "FAILED", r.details.c_str());
        ok &= r.ok;
        if (r.ok) {
            std::string file = "/tmp/bismuth_" + std::to_string(&r - results.data()) + ".ppm";
            savePPM(file, r.image);
        }
    }

    // Sanity: something was drawn.
    if (results[1].ok) {
        u32 changed = 0;
        auto& img = results[1].image;
        for (size_t i = 0; i < img.size(); i += 4)
            if (img[i] != 26 || img[i + 1] != 51 || img[i + 2] != 77) changed++;
        std::printf("pixels covered by sprites: %u of %u\n", changed, W * H);
        ok &= changed > W * H / 20;
    }

    for (size_t i = 0; i < results.size(); i++)
        for (size_t j = i + 1; j < results.size(); j++)
            if (results[i].ok && results[j].ok)
                ok &= compareImages(results[i], results[j], 3);

    std::printf("render_test: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
