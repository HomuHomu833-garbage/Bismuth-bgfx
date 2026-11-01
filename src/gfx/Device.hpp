#pragma once

/*
    bgfx lifecycle inside Geometry Dash.

    Backend order (BackendPreference::Auto):
      1. Native: Vulkan (Windows, Android) or Metal (macOS, iOS), rendering
         offscreen into textures shared with GD's OpenGL context (see Interop.hpp).
      2. OpenGL / OpenGL ES fallback: bgfx renders directly inside GD's own GL
         context ("host mode"), flushing at every point where Bismuth draws.

    Independently of the backend, sprite culling runs either on the GPU (compute
    shaders) or on the CPU (multithreaded, same kernel) depending on capabilities.
*/

#include <core/Types.hpp>
#include <bgfx/bgfx.h>
#include "HostGL.hpp"
#include "Interop.hpp"
#include "HostTextures.hpp"
#include <string>

namespace bismuth::gfx {

enum class Backend { None, Vulkan, Metal, OpenGL };
enum class BackendPreference { Auto, Native, OpenGL };
enum class CullingPreference { Auto, Gpu, Cpu };
enum class CullingPath { Gpu, Cpu };

const char* backendName(Backend backend);

struct DeviceOptions {
    BackendPreference backend = BackendPreference::Auto;
    CullingPreference culling = CullingPreference::Auto;
};

class Device {
public:
    static Device& get();

    /// Initializes bgfx. GD's GL context must be current. Returns false if no
    /// backend could be initialized; `report` describes every attempt.
    bool init(const DeviceOptions& options, std::string& report);

    /// Shuts bgfx down. If the host GL context was lost, GL objects aren't deleted.
    void shutdown();

    bool isInitialized() const { return backend != Backend::None; }
    Backend getBackend() const { return backend; }
    CullingPath getCullingPath() const { return cullingPath; }
    bool isHostMode() const { return backend == Backend::OpenGL; }
    bgfx::RendererType::Enum getRendererType() const { return rendererType; }

    const HostGLInfo& getHost() const { return host; }
    Interop* getInterop() const { return interop.get(); }
    Compositor& getCompositor() { return compositor; }

    /// GD textures imported into bgfx (sprite sheets). Kept for the whole
    /// session: importing them is slow with Vulkan / Metal (GPU readback).
    /// Emptied when the device shuts down.
    HostTextures& getHostTextures() { return hostTextures; }

    /// Changes every time the device shuts down, invalidating every bgfx handle.
    u32 getGeneration() const { return generation; }

    /// True if GD's GL context changed since init (Android can destroy it).
    bool isHostContextLost();

    /// Host mode: submits everything queued so far into GD's currently bound
    /// framebuffer, then restores GD's GL state.
    void flushHost();

    /// Host mode: view rect that maps to GD's current GL viewport.
    void setHostViewRect(bgfx::ViewId view);

    /// Native mode: renders everything queued so far and publishes the shared
    /// targets to GL.
    void submitNative();

    /// Number returned by the last bgfx::frame().
    u32 getLastFrameNumber() const { return lastFrameNumber; }

    /// GPU time (ms) of the bgfx frames submitted during the last game frame.
    double getLastGpuTimeMs() const { return lastGpuTimeMs; }
    void endGameFrame();

    std::string describe() const;

private:
    bool initNative(std::string& report);
    bool initHostGL(std::string& report);
    void chooseCullingPath(CullingPreference preference);
    void accumulateStats();

    Backend backend = Backend::None;
    CullingPath cullingPath = CullingPath::Cpu;
    bgfx::RendererType::Enum rendererType = bgfx::RendererType::Noop;
    HostGLInfo host;
    UPtr<Interop> interop;
    Compositor compositor;
    HostTextures hostTextures;
    u32 generation = 0;
    bool hostLost = false;

    u32 lastFrameNumber = 0;
    double frameGpuTimeMs = 0.0;
    double lastGpuTimeMs = 0.0;
    std::string cullingReason;
};

/// Width/height used for bgfx's (unused) back buffer in host mode. Viewport Y
/// is flipped against it, so it must exceed any framebuffer GD renders to.
constexpr u32 kHostResolution = 16384;

} // namespace bismuth::gfx
