#include "CpuCuller.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>

namespace bismuth {

//// THREAD POOL ////

ParallelFor::ParallelFor(u32 threadCount) {
    if (threadCount == 0) {
        u32 hw = std::thread::hardware_concurrency();
        // Leave a core for the game's main thread work and the driver.
        threadCount = hw > 2 ? hw - 1 : 1;
        threadCount = std::min<u32>(threadCount, 16);
    }

    for (u32 i = 1; i < threadCount; i++)
        workers.emplace_back(&ParallelFor::workerMain, this);
}

ParallelFor::~ParallelFor() {
    {
        std::scoped_lock lock(mutex);
        quit = true;
    }
    wake.notify_all();
    for (auto& worker : workers)
        worker.join();
}

void ParallelFor::work() {
    while (true) {
        u32 begin = nextIndex.fetch_add(jobChunk, std::memory_order_relaxed);
        if (begin >= jobCount)
            break;
        (*job)(begin, std::min(begin + jobChunk, jobCount));
    }
}

void ParallelFor::workerMain() {
    u64 seenGeneration = 0;
    while (true) {
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, [&] { return quit || generation != seenGeneration; });
            if (quit)
                return;
            seenGeneration = generation;
        }

        work();

        if (activeWorkers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::scoped_lock lock(mutex);
            done.notify_all();
        }
    }
}

void ParallelFor::run(u32 count, u32 chunkSize, const std::function<void(u32, u32)>& fn) {
    if (count == 0)
        return;

    chunkSize = std::max<u32>(chunkSize, 1);

    if (workers.empty() || count <= chunkSize) {
        fn(0, count);
        return;
    }

    {
        std::scoped_lock lock(mutex);
        job = &fn;
        jobCount = count;
        jobChunk = chunkSize;
        nextIndex.store(0, std::memory_order_relaxed);
        activeWorkers.store((u32)workers.size(), std::memory_order_relaxed);
        generation++;
    }
    wake.notify_all();

    work();

    std::unique_lock lock(mutex);
    done.wait(lock, [&] { return activeWorkers.load(std::memory_order_acquire) == 0; });
    job = nullptr;
}

//// KERNEL ////

namespace {

/*
    The shared shader kernel compiled as C++: the file is included inside this
    struct so that its "global" helpers bkColorChannel / bkPulseEntry can read
    the frame buffer through member variables.
*/
struct CpuKernel {
    using vec2  = glm::vec2;
    using vec3  = glm::vec3;
    using vec4  = glm::vec4;
    using uvec2 = glm::uvec2;
    using uvec4 = glm::uvec4;
    using uint  = u32;

    const UVec4* frame = nullptr;
    u32 frameSize = 0;
    u32 pulseOffset = 0;

    // GLSL built-ins used by the kernel.
    template <typename... A> static auto mix(A... a)   { return glm::mix(a...); }
    template <typename... A> static auto clamp(A... a) { return glm::clamp(a...); }
    template <typename... A> static auto step(A... a)  { return glm::step(a...); }
    template <typename T> static T abs(T x)   { return glm::abs(x); }
    template <typename T> static T fract(T x) { return glm::fract(x); }
    template <typename T> static T floor(T x) { return glm::floor(x); }
    static float mod(float a, float b) { return a - b * std::floor(a / b); }
    static float min(float a, float b) { return a < b ? a : b; }
    static float max(float a, float b) { return a > b ? a : b; }
    static float sin(float x) { return std::sin(x); }
    static float cos(float x) { return std::cos(x); }
    static float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
    static float uintBitsToFloat(u32 v) { return bitsFloat(v); }

    uint bkColorChannel(uint id) const {
        uint index = id / 4u;
        if (index >= frameSize) return 0;
        const UVec4& v = frame[index];
        return v[id % 4u];
    }

    uvec2 bkPulseEntry(uint index) const {
        uint i = pulseOffset + index / 2u;
        if (i >= frameSize) return uvec2(0u, 0u);
        const UVec4& v = frame[i];
        return (index % 2u) == 0u ? uvec2(v.x, v.y) : uvec2(v.z, v.w);
    }

#include "../../shaders/bismuth_kernel.sh"
};

} // namespace

//// CULLER ////

void CpuCuller::run(
    const StaticBatchData& staticData,
    const SortedBatchData& sorted,
    const FrameBatchData& frame,
    const GameStateUniforms& gameState,
    std::vector<CpuVertex>& outVertices,
    std::vector<LayerVertexRange>& outLayers
) {
    const u32 workGroupCount = sorted.workGroupCount;
    const u32 maxVertices = staticData.triangleCount * 3;

    if (scratch.size() < maxVertices)
        scratch.resize(maxVertices);
    workGroupCounts.assign(workGroupCount, 0);
    workGroupOffsets.assign(workGroupCount, 0);

    CpuKernel kernel;
    kernel.frame = frame.buffer.data();
    kernel.frameSize = (u32)frame.buffer.size();
    kernel.pulseOffset = frame.pulseOffset;

    CpuKernel::BkGameState gs;
    gs.timer               = gameState.timer;
    gs.audioScale          = gameState.audioScale;
    gs.cameraViewSize      = gameState.cameraViewSize;
    gs.cameraPosition      = gameState.cameraPosition;
    gs.cameraRight         = gameState.cameraRight;
    gs.cameraUp            = gameState.cameraUp;
    gs.cameraSize          = gameState.cameraSize;
    gs.winSize             = gameState.winSize;
    gs.screenRight         = gameState.screenRight;
    gs.cameraUnzoomedX     = gameState.cameraUnzoomedX;
    gs.specialLightBGColor = gameState.specialLightBGColor;
    gs.flags               = gameState.flags;

    const UVec4* sortedData = sorted.buffer.data();
    const UVec4* staticBuf = staticData.staticBuffer.data();
    const PackedVertex* vertices = staticData.vertices.data();
    const UVec4* frameData = frame.buffer.data();
    const u32 frameSize = (u32)frame.buffer.size();

    //// PASS 1: cull and transform (cs_cull) ////
    pool.run(workGroupCount, 8, [&](u32 begin, u32 end) {
        for (u32 w = begin; w < end; w++) {
            const UVec4 workGroup = sortedData[sorted.workGroupOffset + w];
            u32 outVertex = workGroup.z * 3;
            const u32 startVertex = outVertex;

            for (u32 i = workGroup.x; i < workGroup.y; i++) {
                const UVec4 entry = sortedData[i];
                glm::vec2 startPosition(bitsFloat(entry.x), bitsFloat(entry.y));
                u32 groupIndex  = entry.z;
                u32 spriteIndex = entry.w;

                u32 g = frame.groupStateOffset + groupIndex * GROUP_STATE_STRIDE;
                if (g + 2 >= frameSize)
                    continue;
                auto state = kernel.bkDecodeGroupState(frameData[g], frameData[g + 1], frameData[g + 2]);

                glm::vec2 objectPosition = kernel.bkObjectPosition(state, startPosition);
                if (!kernel.bkIsOnScreen(gs, objectPosition))
                    continue;

                const UVec4 sprite = staticBuf[staticData.inputSpriteOffset + spriteIndex];
                u32 o = sprite.x * OBJECT_INFO_STRIDE;
                auto object = kernel.bkDecodeObjectInfo(staticBuf[o], staticBuf[o + 1], staticBuf[o + 2]);

                auto result = kernel.bkProcessSprite(gs, state, object, sprite.y, objectPosition);
                if (!result.visible)
                    continue;

                const u32 color = kernel.bkPackColor(result.color);
                const glm::vec4 m = result.localTransform;

                const u32 firstVertex = sprite.z * 3;
                const u32 endVertex = firstVertex + sprite.w * 3;
                for (u32 v = firstVertex; v < endVertex; v++) {
                    const PackedVertex& pv = vertices[v];
                    const glm::vec2 offset(pv.positionOffsetX, pv.positionOffsetY);
                    const glm::vec2 uv = unpackTexCoord(pv.texCoord);

                    CpuVertex& out = scratch[outVertex++];
                    out.x = objectPosition.x + m.x * offset.x + m.z * offset.y;
                    out.y = objectPosition.y + m.y * offset.x + m.w * offset.y;
                    out.u = uv.x;
                    out.v = uv.y;
                    out.color = color;
                }
            }

            workGroupCounts[w] = outVertex - startVertex;
        }
    });

    //// PASS 2: per-layer prefix sums (cs_scan) ////
    outLayers.assign(sorted.layerCount, {});
    u32 totalVertices = 0;
    for (u32 l = 0; l < sorted.layerCount; l++) {
        const UVec4 layer = sortedData[sorted.layerOffset + l];
        outLayers[l].firstVertex = totalVertices;
        for (u32 w = layer.x; w < layer.x + layer.y; w++) {
            workGroupOffsets[w] = totalVertices;
            totalVertices += workGroupCounts[w];
        }
        outLayers[l].vertexCount = totalVertices - outLayers[l].firstVertex;
    }

    //// PASS 3: compaction (cs_copy) ////
    outVertices.resize(totalVertices);
    pool.run(workGroupCount, 32, [&](u32 begin, u32 end) {
        for (u32 w = begin; w < end; w++) {
            u32 count = workGroupCounts[w];
            if (count == 0)
                continue;
            const UVec4 workGroup = sortedData[sorted.workGroupOffset + w];
            std::memcpy(&outVertices[workGroupOffsets[w]], &scratch[workGroup.z * 3], count * sizeof(CpuVertex));
        }
    });
}

} // namespace bismuth
