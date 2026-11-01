#include <cstring>
#include "BatchRenderer.hpp"
#include <gfx/Programs.hpp>

#include <algorithm>


namespace {

/// Gives a vector's contents to bgfx without copying (freed once uploaded).
/// Empty vectors become one zeroed uvec4, as buffers can't be empty.
template <class T>
const bgfx::Memory* moveToBgfx(std::vector<T>& vector) {
    if (vector.empty()) {
        const bgfx::Memory* memory = bgfx::alloc(sizeof(bismuth::UVec4));
        std::memset(memory->data, 0, memory->size);
        return memory;
    }

    auto* owned = new std::vector<T>(std::move(vector));
    vector = {};
    return bgfx::makeRef(
        owned->data(), (u32)(owned->size() * sizeof(T)),
        [](void*, void* userData) { delete static_cast<std::vector<T>*>(userData); },
        owned
    );
}

} // namespace

namespace bismuth {

template <typename H>
static void destroyHandle(H& handle) {
    if (bgfx::isValid(handle))
        bgfx::destroy(handle);
    handle = BGFX_INVALID_HANDLE;
}

BatchRenderer::~BatchRenderer() {
    shutdown();
}

bool BatchRenderer::init(StaticBatchData staticData, gfx::CullingPath cullingPath, bgfx::RendererType::Enum renderer) {
    shutdown();

    data = std::move(staticData);
    path = cullingPath;

    const bool gpu = path == gfx::CullingPath::Gpu;
    const auto profile = gfx::shaderProfileFor(renderer, gpu);

    uvec4Layout.begin()
        .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
        .end();

    cpuVertexLayout.begin()
        .add(bgfx::Attrib::Position,  2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
        .end();

    u_gameState   = bgfx::createUniform("u_gameState", bgfx::UniformType::Vec4, GAME_STATE_VEC4S);
    u_mvp         = bgfx::createUniform("u_bismuthMvp", bgfx::UniformType::Mat4);
    s_spriteSheet = bgfx::createUniform("s_spriteSheet", bgfx::UniformType::Sampler);

    if (gpu) {
        drawProgram     = gfx::loadProgram("vs_object_gpu", "fs_object", profile);
        overdrawProgram = gfx::loadProgram("vs_object_gpu", "fs_overdraw_count", profile);
        cullProgram     = gfx::loadComputeProgram("cs_cull", profile);
        scanProgram     = gfx::loadComputeProgram("cs_scan", profile);
        copyProgram     = gfx::loadComputeProgram("cs_copy", profile);

        if (!bgfx::isValid(cullProgram) || !bgfx::isValid(scanProgram) || !bgfx::isValid(copyProgram))
            return false;

        // The CPU copies aren't needed after this: hand them to bgfx instead of
        // copying them (they reach hundreds of MB on very large levels).
        const u32 staticSize = (u32)(data.staticBuffer.size() * sizeof(UVec4));
        staticBuffer = bgfx::createVertexBuffer(moveToBgfx(data.staticBuffer), uvec4Layout, BGFX_BUFFER_COMPUTE_READ);

        const u32 vertexSize = (u32)(data.vertices.size() * sizeof(PackedVertex));
        vertexBuffer = bgfx::createVertexBuffer(moveToBgfx(data.vertices), uvec4Layout, BGFX_BUFFER_COMPUTE_READ);

        const u32 triangles = std::max<u32>(data.triangleCount, 1);
        culledTris  = bgfx::createDynamicIndexBuffer(triangles, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
        compactTris = bgfx::createDynamicIndexBuffer(triangles, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
        rawSprites  = bgfx::createDynamicVertexBuffer(
            std::max<u32>(data.inputSpriteCount, 1) * RAW_SPRITE_STRIDE, uvec4Layout, BGFX_BUFFER_COMPUTE_READ_WRITE);

        staticBytes = staticSize + vertexSize + (u64)triangles * 8 + (u64)data.inputSpriteCount * RAW_SPRITE_STRIDE * 16;

        logf(LogLevel::Info,
            "Bismuth: GPU buffers: objects %.1f MB, vertices %.1f MB, triangles 2 x %.1f MB, sprites %.1f MB",
            staticSize / 1048576.0, vertexSize / 1048576.0, triangles * 4 / 1048576.0,
            (double)data.inputSpriteCount * RAW_SPRITE_STRIDE * 16 / 1048576.0);

    } else {
        drawProgram     = gfx::loadProgram("vs_object_cpu", "fs_object", profile);
        overdrawProgram = gfx::loadProgram("vs_object_cpu", "fs_overdraw_count", profile);
        culler = makeUnique<CpuCuller>();
        staticBytes = data.staticBuffer.size() * sizeof(UVec4) + data.vertices.size() * sizeof(PackedVertex);
    }

    if (!bgfx::isValid(drawProgram))
        return false;

    initialized = true;
    return true;
}

void BatchRenderer::shutdown() {
    for (auto& [_, gpuBatch] : gpuBatches) {
        destroyHandle(gpuBatch.buffer);
        destroyHandle(gpuBatch.indirect);
    }
    gpuBatches.clear();

    destroyHandle(u_gameState);
    destroyHandle(u_mvp);
    destroyHandle(s_spriteSheet);
    destroyHandle(drawProgram);
    destroyHandle(overdrawProgram);
    destroyHandle(cullProgram);
    destroyHandle(scanProgram);
    destroyHandle(copyProgram);
    destroyHandle(staticBuffer);
    destroyHandle(vertexBuffer);
    destroyHandle(frameBuffer);
    destroyHandle(culledTris);
    destroyHandle(compactTris);
    destroyHandle(rawSprites);
    destroyHandle(counters);
    destroyHandle(cpuVertexBuffer);
    frameBufferCapacity = 0;
    counterCapacity = 0;
    cpuVertexCapacity = 0;
    culler = nullptr;
    batch = nullptr;
    frame = nullptr;
    initialized = false;
}

//// BATCHES ////

BatchRenderer::GpuBatch& BatchRenderer::getGpuBatch(const SortedBatchData* sorted) {
    auto it = gpuBatches.find(sorted);
    if (it != gpuBatches.end())
        return it->second;

    GpuBatch& gpuBatch = gpuBatches[sorted];
    const u32 size = (u32)(sorted->buffer.size() * sizeof(UVec4));
    gpuBatch.buffer = bgfx::createVertexBuffer(
        bgfx::copy(sorted->buffer.data(), std::max<u32>(size, sizeof(UVec4))),
        uvec4Layout, BGFX_BUFFER_COMPUTE_READ);
    gpuBatch.indirect = bgfx::createIndirectBuffer(std::max<u32>(sorted->layerCount, 1));
    return gpuBatch;
}

void BatchRenderer::setSortedBatch(const SortedBatchData* sorted) {
    batch = sorted;
    if (batch && path == gfx::CullingPath::Gpu) {
        getGpuBatch(batch);
        ensureCounterBuffer(batch->workGroupCount);
    }
}

void BatchRenderer::forgetSortedBatch(const SortedBatchData* sorted) {
    auto it = gpuBatches.find(sorted);
    if (it != gpuBatches.end()) {
        destroyHandle(it->second.buffer);
        destroyHandle(it->second.indirect);
        gpuBatches.erase(it);
    }
    if (batch == sorted)
        batch = nullptr;
}

bool BatchRenderer::hasLayer(u32 layerIndex) const {
    return batch && layerIndex < batch->layerCount;
}

//// FRAME ////

void BatchRenderer::ensureFrameBuffer(u32 elements) {
    if (bgfx::isValid(frameBuffer) && frameBufferCapacity >= elements)
        return;
    destroyHandle(frameBuffer);
    frameBufferCapacity = std::max<u32>(elements + elements / 2, 1024);
    frameBuffer = bgfx::createDynamicVertexBuffer(frameBufferCapacity, uvec4Layout, BGFX_BUFFER_COMPUTE_READ);
}

void BatchRenderer::ensureCounterBuffer(u32 workGroups) {
    const u32 needed = std::max<u32>(workGroups, 1) * WORK_GROUP_COUNTER_STRIDE;
    if (bgfx::isValid(counters) && counterCapacity >= needed)
        return;
    destroyHandle(counters);
    counterCapacity = needed;
    counters = bgfx::createDynamicIndexBuffer(counterCapacity, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
}

void BatchRenderer::updateFrame(const FrameBatchData& frameData, const GameStateUniforms& state) {
    gameState = state;
    groupStateOffset = frameData.groupStateOffset;
    pulseOffset = frameData.pulseOffset;
    frameBytes = frameData.buffer.size() * sizeof(UVec4);

    if (path == gfx::CullingPath::Gpu) {
        const u32 elements = std::max<u32>((u32)frameData.buffer.size(), 1);
        ensureFrameBuffer(elements);
        bgfx::update(frameBuffer, 0, bgfx::copy(frameData.buffer.data(), elements * sizeof(UVec4)));
    } else {
        frame = &frameData;
    }
}

void BatchRenderer::setGameStateUniform() {
    const auto& gs = gameState;
    float values[GAME_STATE_VEC4S * 4] = {
        gs.timer, gs.audioScale, gs.cameraViewSize.x, gs.cameraViewSize.y,
        gs.cameraPosition.x, gs.cameraPosition.y, gs.cameraRight.x, gs.cameraRight.y,
        gs.cameraUp.x, gs.cameraUp.y, gs.cameraSize.x, gs.cameraSize.y,
        gs.winSize.x, gs.winSize.y, gs.screenRight, gs.cameraUnzoomedX,
        gs.specialLightBGColor.x, gs.specialLightBGColor.y, gs.specialLightBGColor.z, float(gs.flags),
        float(groupStateOffset), float(pulseOffset), float(data.inputSpriteOffset), float(batch ? batch->workGroupOffset : 0),
        float(batch ? batch->layerOffset : 0), float(batch ? batch->workGroupCount : 0), float(batch ? batch->layerCount : 0), 0.f,
    };
    bgfx::setUniform(u_gameState, values, GAME_STATE_VEC4S);
}

void BatchRenderer::cull(bgfx::ViewId computeView) {
    if (!initialized || !batch)
        return;

    if (path == gfx::CullingPath::Cpu) {
        if (!frame)
            return;

        culler->run(data, *batch, *frame, gameState, cpuVertices, cpuLayers);
        lastVisibleVertices = (u32)cpuVertices.size();

        const u32 count = std::max<u32>((u32)cpuVertices.size(), 1);
        if (!bgfx::isValid(cpuVertexBuffer) || cpuVertexCapacity < count) {
            destroyHandle(cpuVertexBuffer);
            cpuVertexCapacity = std::max<u32>(count + count / 2, 4096);
            cpuVertexBuffer = bgfx::createDynamicVertexBuffer(cpuVertexCapacity, cpuVertexLayout);
        }
        if (!cpuVertices.empty()) {
            // The vector stays untouched until the next cull, after this frame is submitted.
            bgfx::update(cpuVertexBuffer, 0, bgfx::makeRef(cpuVertices.data(), (u32)(cpuVertices.size() * sizeof(CpuVertex))));
        }
        return;
    }

    GpuBatch& gpuBatch = getGpuBatch(batch);
    ensureCounterBuffer(batch->workGroupCount);

    if (batch->workGroupCount == 0)
        return;

    // Pass 1: cull + transform
    setGameStateUniform();
    bgfx::setBuffer(0, staticBuffer, bgfx::Access::Read);
    bgfx::setBuffer(1, gpuBatch.buffer, bgfx::Access::Read);
    bgfx::setBuffer(2, frameBuffer, bgfx::Access::Read);
    bgfx::setBuffer(3, culledTris, bgfx::Access::Write);
    bgfx::setBuffer(4, rawSprites, bgfx::Access::Write);
    bgfx::setBuffer(5, counters, bgfx::Access::Write);
    bgfx::dispatch(computeView, cullProgram, batch->workGroupCount, 1, 1);

    // Pass 2: per-layer prefix sums + indirect draw commands
    setGameStateUniform();
    bgfx::setBuffer(0, gpuBatch.buffer, bgfx::Access::Read);
    bgfx::setBuffer(1, counters, bgfx::Access::ReadWrite);
    bgfx::setBuffer(2, gpuBatch.indirect, bgfx::Access::Write);
    bgfx::dispatch(computeView, scanProgram, std::max<u32>(batch->layerCount, 1), 1, 1);

    // Pass 3: compaction
    setGameStateUniform();
    bgfx::setBuffer(0, gpuBatch.buffer, bgfx::Access::Read);
    bgfx::setBuffer(1, counters, bgfx::Access::Read);
    bgfx::setBuffer(2, culledTris, bgfx::Access::Read);
    bgfx::setBuffer(3, compactTris, bgfx::Access::Write);
    bgfx::dispatch(computeView, copyProgram, batch->workGroupCount, 1, 1);
}

//// DRAWING ////

u64 BatchRenderer::blendState(LayerBlend blend, bool toSharedTarget) {
    u64 state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    if (blend == LayerBlend::Normal) {
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    } else if (!toSharedTarget) {
        // Exactly what cocos2d uses: glBlendFunc(GL_SRC_ALPHA, GL_ONE).
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
    } else {
        // In a transparent layer target, additive sprites must not change the
        // target's coverage (alpha), so compositing it with ONE, ONE_MINUS_SRC_ALPHA
        // equals drawing the sprites directly.
        state |= BGFX_STATE_BLEND_FUNC_SEPARATE(
            BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
            BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);
    }
    return state;
}

void BatchRenderer::drawLayer(
    bgfx::ViewId view,
    u32 layerIndex,
    bgfx::TextureHandle texture,
    LayerBlend blend,
    bool toSharedTarget,
    bool overdrawCount
) {
    if (!initialized || !hasLayer(layerIndex))
        return;

    bgfx::ProgramHandle program = overdrawCount ? overdrawProgram : drawProgram;
    if (!bgfx::isValid(program))
        return;

    u64 state = overdrawCount
        ? (BGFX_STATE_WRITE_R | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE))
        : blendState(blend, toSharedTarget);

    if (path == gfx::CullingPath::Cpu) {
        if (layerIndex >= cpuLayers.size() || cpuLayers[layerIndex].vertexCount == 0 || !bgfx::isValid(cpuVertexBuffer))
            return;

        const auto& range = cpuLayers[layerIndex];
        bgfx::setUniform(u_mvp, gameState.mvp);
        bgfx::setTexture(0, s_spriteSheet, texture, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        bgfx::setVertexBuffer(0, cpuVertexBuffer, range.firstVertex, range.vertexCount);
        bgfx::setState(state);
        bgfx::submit(view, program);
        return;
    }

    GpuBatch& gpuBatch = getGpuBatch(batch);

    bgfx::setUniform(u_mvp, gameState.mvp);
    bgfx::setTexture(0, s_spriteSheet, texture, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::setBuffer(1, vertexBuffer, bgfx::Access::Read);
    bgfx::setBuffer(2, compactTris, bgfx::Access::Read);
    bgfx::setBuffer(3, rawSprites, bgfx::Access::Read);
    // Attribute-less draw: vertices are pulled from storage buffers.
    bgfx::setVertexCount(3);
    bgfx::setState(state);
    bgfx::submit(view, program, gpuBatch.indirect, (u16)layerIndex, 1);
}

} // namespace bismuth
