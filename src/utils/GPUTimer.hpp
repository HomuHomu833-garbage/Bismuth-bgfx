#pragma once

#include <common.hpp>
#include <profiler.hpp>

/*
    Named timer shown in the profiler snapshot.

    GPU work is submitted through bgfx, which measures GPU time per frame
    (see gfx::Device::getLastGpuTimeMs), so this only measures CPU time spent
    recording/submitting the work.
*/
class GPUTimer {
public:
    inline GPUTimer(const char* name)
        : name(name)
    {
        profiler::addTimer(this);
    }

    inline ~GPUTimer() {
        profiler::removeTimer(this);
    }

    inline void start() {
        if (profiler::isTakingGPUTime)
            startTime = ::getTime();
    }

    inline void stop() {
        if (profiler::isTakingGPUTime)
            time = (i64)(::getTime() - startTime);
    }

    inline const char* getName() const { return name; }
    inline i64 getTime() const { return time; }

private:
    i64 time = 0;
    u64 startTime = 0;
    const char* name;
};
