#pragma once

#include "BatchData.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace bismuth {

/// Fully transformed vertex produced by the CPU culling path.
struct CpuVertex {
    float x, y;
    float u, v;
    u32 color; // RGBA8, premultiplied
};
static_assert(sizeof(CpuVertex) == 20);

struct LayerVertexRange {
    u32 firstVertex = 0;
    u32 vertexCount = 0;
};

/// Small persistent thread pool for data-parallel loops.
class ParallelFor {
public:
    explicit ParallelFor(u32 threadCount = 0);
    ~ParallelFor();

    ParallelFor(const ParallelFor&) = delete;
    ParallelFor& operator=(const ParallelFor&) = delete;

    /// Calls fn(i) for every i in [0, count), split in chunks over all threads
    /// (the calling thread participates). Blocks until done.
    void run(u32 count, u32 chunkSize, const std::function<void(u32 begin, u32 end)>& fn);

    u32 getThreadCount() const { return (u32)workers.size() + 1; }

private:
    void workerMain();
    void work();

    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable done;
    bool quit = false;
    u64 generation = 0;

    const std::function<void(u32, u32)>* job = nullptr;
    u32 jobCount = 0;
    u32 jobChunk = 1;
    std::atomic<u32> nextIndex { 0 };
    std::atomic<u32> activeWorkers { 0 };
};

/*
    CPU implementation of the culling compute shaders (cs_cull + cs_scan + cs_copy).
    Uses the same kernel (shaders/bismuth_kernel.sh) and the same buffers as the
    GPU path, and produces one contiguous vertex range per layer.
*/
class CpuCuller {
public:
    explicit CpuCuller(u32 threadCount = 0) : pool(threadCount) {}

    void run(
        const StaticBatchData& staticData,
        const SortedBatchData& sorted,
        const FrameBatchData& frame,
        const GameStateUniforms& gameState,
        std::vector<CpuVertex>& outVertices,
        std::vector<LayerVertexRange>& outLayers
    );

    u32 getThreadCount() const { return pool.getThreadCount(); }

private:
    ParallelFor pool;
    std::vector<CpuVertex> scratch;
    std::vector<u32> workGroupCounts;
    std::vector<u32> workGroupOffsets;
};

} // namespace bismuth
