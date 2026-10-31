#include "OverdrawView.hpp"
#include "HotKey.hpp"
#include "Renderer.hpp"
#include <gfx/Device.hpp>
#include <gfx/Programs.hpp>

using namespace geode::prelude;
using namespace bismuth;
namespace gfx = bismuth::gfx;

static OverdrawView* instance = nullptr;

class OverdrawPostDrawNode : public CCNode {
public:
    void draw() override {
        if (instance)
            instance->postDraw();
    }
};

OverdrawView::OverdrawView(Renderer& renderer)
    : renderer(renderer)
{
    instance = this;
}

OverdrawView::~OverdrawView() {
    instance = nullptr;
    releaseResources();
}

void OverdrawView::setEnabled(bool enabled) {
    this->enabled = enabled;

    if (enabled && !postNode) {
        auto node = new OverdrawPostDrawNode();
        node->autorelease();
        renderer.getPlayLayer()->m_objectLayer->addChild(node, 10000);
        postNode = node;
    }
}

void OverdrawView::releaseResources() {
    auto destroy = [](auto& handle) {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    };
    destroy(counter);
    destroy(counterTexture);
    destroy(readbackTexture);
    destroy(heatProgram);
    destroy(s_overdraw);
    destroy(quad);
    destroy(quadFlipped);
    width = height = 0;
    readbackPending = false;
}

bool OverdrawView::ensureResources(u32 w, u32 h) {
    auto& device = gfx::Device::get();

    if (!bgfx::isValid(heatProgram)) {
        heatProgram = gfx::loadProgram("vs_fullscreen", "fs_overdraw_heat",
                                       gfx::shaderProfileFor(device.getRendererType(), false));
        s_overdraw = bgfx::createUniform("s_overdraw", bgfx::UniformType::Sampler);

        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float).end();
        static const float positions[] = { -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f };
        // Same quad with Y mirrored: flips the sampled texture coordinates for
        // backends whose framebuffers have a top-left origin.
        static const float flipped[]   = { -1.f, 1.f, 1.f, 1.f, -1.f, -1.f, 1.f, -1.f };
        quad = bgfx::createVertexBuffer(bgfx::makeRef(positions, sizeof(positions)), layout);
        quadFlipped = bgfx::createVertexBuffer(bgfx::makeRef(flipped, sizeof(flipped)), layout);
    }

    if (w != width || h != height || !bgfx::isValid(counter)) {
        if (bgfx::isValid(counter)) bgfx::destroy(counter);
        if (bgfx::isValid(counterTexture)) bgfx::destroy(counterTexture);
        if (bgfx::isValid(readbackTexture)) bgfx::destroy(readbackTexture);

        width = w;
        height = h;
        counterTexture = bgfx::createTexture2D((u16)w, (u16)h, false, 1, bgfx::TextureFormat::RGBA8,
                                               BGFX_TEXTURE_RT | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
        counter = bgfx::createFrameBuffer(1, &counterTexture, false);

        const auto caps = bgfx::getCaps()->supported;
        readbackTexture = BGFX_INVALID_HANDLE;
        if ((caps & BGFX_CAPS_TEXTURE_BLIT) && (caps & BGFX_CAPS_TEXTURE_READ_BACK))
            readbackTexture = bgfx::createTexture2D((u16)w, (u16)h, false, 1, bgfx::TextureFormat::RGBA8,
                                                    BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        readbackPending = false;
    }

    return bgfx::isValid(heatProgram) && bgfx::isValid(counter);
}

void OverdrawView::beginFrame(u32 w, u32 h) {
    if (!enabled)
        return;

    auto& device = gfx::Device::get();

    if (device.isHostMode()) {
        gfx::GLint viewport[4] = {};
        gfx::glf().glGetIntegerv(gfx::gl::VIEWPORT, viewport);
        w = (u32)std::max(viewport[2], 1);
        h = (u32)std::max(viewport[3], 1);
    }

    if (!ensureResources(w, h))
        return;

    pollReadback();

    bgfx::setViewFrameBuffer(RendererViews::OverdrawCount, counter);
    bgfx::setViewRect(RendererViews::OverdrawCount, 0, 0, (u16)w, (u16)h);
    bgfx::setViewClear(RendererViews::OverdrawCount, BGFX_CLEAR_COLOR, 0x00000000);
    bgfx::touch(RendererViews::OverdrawCount);

    if (device.isHostMode()) {
        // Host mode submits once per composite group: clear now, accumulate after.
        device.flushHost();
        bgfx::setViewClear(RendererViews::OverdrawCount, BGFX_CLEAR_NONE);
    }
}

void OverdrawView::countLayer(u32 layerIndex, bgfx::TextureHandle texture) {
    if (!enabled || !bgfx::isValid(counter))
        return;
    renderer.getBatchRenderer().drawLayer(RendererViews::OverdrawCount, layerIndex, texture, LayerBlend::Normal, false, true);
}

void OverdrawView::submitHeatMap(bgfx::ViewId view, bool flipY) {
    bgfx::setTexture(0, s_overdraw, counterTexture);
    bgfx::setVertexBuffer(0, flipY ? quadFlipped : quad);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_PT_TRISTRIP
        | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(view, heatProgram);

    // Copy the counts for the overdraw rate, every so often.
    if (bgfx::isValid(readbackTexture) && !readbackPending && ++framesSinceReadback >= 30) {
        framesSinceReadback = 0;
        bgfx::blit(view, readbackTexture, 0, 0, counterTexture);
        readbackData.resize((size_t)width * height * 4);
        readbackReadyFrame = bgfx::readTexture(readbackTexture, readbackData.data());
        readbackPending = true;
    }
}

void OverdrawView::renderHeatMap(gfx::SharedTarget* target, u32 w, u32 h) {
    if (!enabled || !target || !bgfx::isValid(counter))
        return;

    bgfx::setViewFrameBuffer(RendererViews::Overlay, target->framebuffer);
    bgfx::setViewRect(RendererViews::Overlay, 0, 0, (u16)w, (u16)h);
    bgfx::setViewClear(RendererViews::Overlay, BGFX_CLEAR_COLOR, 0x00000000);
    // Counter and overlay share the native backend's top-left origin.
    submitHeatMap(RendererViews::Overlay, !bgfx::getCaps()->originBottomLeft);
}

void OverdrawView::postDraw() {
    if (!enabled || !bgfx::isValid(counter))
        return;

    auto& device = gfx::Device::get();
    if (!device.isHostMode() || !renderer.isActive() || !renderer.isEnabled())
        return; // native backends: composited by Renderer::finish

    device.setHostViewRect(RendererViews::Overlay);
    bgfx::setViewFrameBuffer(RendererViews::Overlay, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(RendererViews::Overlay, BGFX_CLEAR_NONE);
    submitHeatMap(RendererViews::Overlay, false);
    device.flushHost();
}

void OverdrawView::pollReadback() {
    if (!readbackPending)
        return;
    if (gfx::Device::get().getLastFrameNumber() < readbackReadyFrame)
        return;

    readbackPending = false;
    u64 total = 0;
    for (size_t i = 0; i < readbackData.size(); i += 4)
        total += readbackData[i];
    overdrawRate = (float)((double)total / ((double)width * (double)height));
    rateAvailable = true;
}

std::string OverdrawView::getDebugText() {
    if (!enabled) return "";

    std::string text = "Overdraw view is enabled, press F4 to hide this screen\n";
    if (rateAvailable)
        text += fmt::format("Bismuth overdraw rate: {:.2f}%\n\n", overdrawRate * 100.f);
    else
        text += "Bismuth overdraw rate: n/a\n\n";
    return text;
}

DECLARE_HOTKEY(KEY_F4, {
    if (instance)
        instance->setEnabled(!instance->isEnabled());
})
