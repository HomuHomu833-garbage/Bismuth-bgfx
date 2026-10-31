#pragma once

#include <common.hpp>
#include <bgfx/bgfx.h>
#include <gfx/Interop.hpp>
#include <vector>

class Renderer;

/*
    Overdraw visualizer (F4 / "Overdraw view" setting).

    Every Bismuth layer is also drawn into a counter target where each rasterized
    fragment adds 1/255, then a heat map of that target is drawn over the level.
    Unlike the original stencil-based version this only counts Bismuth's own
    layers (reading back stencil isn't possible on OpenGL ES / Vulkan sharing).
*/
class OverdrawView {
public:
    OverdrawView(Renderer& renderer);
    ~OverdrawView();

    void setEnabled(bool enabled);
    inline bool isEnabled() const { return enabled; }

    bool isOwnNode(cocos2d::CCNode* node) const { return node == postNode; }

    /// Clears the counter. Host mode: queries the GL viewport for the size.
    void beginFrame(u32 width = 0, u32 height = 0);

    void countLayer(u32 layerIndex, bgfx::TextureHandle texture);

    /// Native backends: renders the heat map into `target` (composited by the renderer).
    void renderHeatMap(bismuth::gfx::SharedTarget* target, u32 width, u32 height);

    /// Called from a node drawn after the object layer's content.
    void postDraw();

    void releaseResources();

    std::string getDebugText();

private:
    bool ensureResources(u32 width, u32 height);
    void submitHeatMap(bgfx::ViewId view, bool flipY);
    void pollReadback();

    Renderer& renderer;

    bool enabled = false;
    cocos2d::CCNode* postNode = nullptr;

    u32 width = 0, height = 0;
    bgfx::TextureHandle counterTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle counter = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readbackTexture = BGFX_INVALID_HANDLE;
    std::vector<u8> readbackData;
    u32 readbackReadyFrame = 0;
    bool readbackPending = false;
    u32 framesSinceReadback = 0;

    bgfx::ProgramHandle heatProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_overdraw = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle quad = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle quadFlipped = BGFX_INVALID_HANDLE;

    float overdrawRate = 0.0f;
    bool rateAvailable = false;
};
