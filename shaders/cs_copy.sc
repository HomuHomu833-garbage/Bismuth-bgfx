/*
    Pass 3/3 of GPU culling: copy every work group's visible triangles into the
    compacted triangle buffer that the vertex shader reads.
*/
#include <bgfx_compute.sh>
#include "bismuth_shared.sh"

BUFFER_RO(b_sorted, uvec4, 0);
BUFFER_RO(b_counters, uint, 1);
BUFFER_RO(b_culledTris, uint, 2);
BUFFER_WR(b_compactTris, uint, 3);

uniform vec4 u_gameState[GAME_STATE_VEC4S];

#define BK_WORK_GROUP_OFFSET uint(u_gameState[5].w)
#define BK_WORK_GROUP_COUNT  uint(u_gameState[6].y)

NUM_THREADS(COPY_THREADS, 1, 1)
void main()
{
    uint workGroupIndex = uint(gl_WorkGroupID.x);
    uint tid = uint(gl_LocalInvocationIndex);
    if (workGroupIndex >= BK_WORK_GROUP_COUNT)
        return;

    uint src   = b_sorted[BK_WORK_GROUP_OFFSET + workGroupIndex].z;
    uint count = b_counters[workGroupIndex * WORK_GROUP_COUNTER_STRIDE];
    uint dst   = b_counters[workGroupIndex * WORK_GROUP_COUNTER_STRIDE + 1u];

    for (uint i = tid; i < count; i += uint(COPY_THREADS))
        b_compactTris[dst + i] = b_culledTris[src + i];
}
