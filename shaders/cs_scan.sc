/*
    Pass 2/3 of GPU culling: one work group per layer computes where every work
    group's visible triangles go in the compacted buffer, and writes the layer's
    single indirect draw command.
*/
#include <bgfx_compute.sh>
#include "bismuth_shared.sh"

BUFFER_RO(b_sorted, uvec4, 0);
BUFFER_RW(b_counters, uint, 1);
BUFFER_WR(b_indirect, uvec4, 2);

uniform vec4 u_gameState[GAME_STATE_VEC4S];

#define BK_LAYER_OFFSET uint(u_gameState[6].x)
#define BK_LAYER_COUNT  uint(u_gameState[6].z)

SHARED uint s_partial[SCAN_THREADS];

NUM_THREADS(SCAN_THREADS, 1, 1)
void main()
{
    uint layerIndex = uint(gl_WorkGroupID.x);
    uint tid = uint(gl_LocalInvocationIndex);

    uvec4 layer = b_sorted[BK_LAYER_OFFSET + min(layerIndex, BK_LAYER_COUNT - 1u)];
    uint firstWorkGroup = layer.x;
    uint workGroupCount = layer.y;
    uint outBase = layer.z;

    uint chunk = (workGroupCount + uint(SCAN_THREADS) - 1u) / uint(SCAN_THREADS);
    uint begin = firstWorkGroup + min(tid * chunk, workGroupCount);
    uint end   = firstWorkGroup + min((tid + 1u) * chunk, workGroupCount);

    uint sum = 0u;
    for (uint w = begin; w < end; w++)
        sum += b_counters[w * WORK_GROUP_COUNTER_STRIDE];

    s_partial[tid] = sum;
    barrier();

    uint prefix = 0u;
    uint total = 0u;
    for (uint k = 0u; k < uint(SCAN_THREADS); k++) {
        uint p = s_partial[k];
        if (k < tid)
            prefix += p;
        total += p;
    }

    uint running = outBase + prefix;
    for (uint w = begin; w < end; w++) {
        b_counters[w * WORK_GROUP_COUNTER_STRIDE + 1u] = running;
        running += b_counters[w * WORK_GROUP_COUNTER_STRIDE];
    }

    if (tid == 0u && layerIndex < BK_LAYER_COUNT)
        drawIndirect(b_indirect, layerIndex, total * 3u, 1u, outBase * 3u, 0u);
}
