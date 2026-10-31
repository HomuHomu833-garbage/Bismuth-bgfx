/*
    Pass 1/3 of GPU culling: for every sprite of a work group, check whether it
    is on screen, compute its transform and color, and append its triangles to
    the work group's private range of the culled triangle buffer.
*/
#include <bgfx_compute.sh>
#include "bismuth_shared.sh"

BUFFER_RO(b_static, uvec4, 0);   // object infos + input sprites
BUFFER_RO(b_sorted, uvec4, 1);   // sorted sprites + work groups + layers
BUFFER_RO(b_frame,  uvec4, 2);   // color channels + group states + pulse entries
BUFFER_WR(b_culledTris, uint, 3);
BUFFER_WR(b_rawSprites, uvec4, 4);
BUFFER_WR(b_counters, uint, 5);

uint bkColorChannel(uint id)
{
    uvec4 v = b_frame[id / 4u];
    uint c = id % 4u;
    return c == 0u ? v.x : (c == 1u ? v.y : (c == 2u ? v.z : v.w));
}

#include "bismuth_uniforms.sh"

uvec2 bkPulseEntry(uint index)
{
    uvec4 v = b_frame[BK_PULSE_OFFSET + index / 2u];
    return (index % 2u) == 0u ? uvec2(v.x, v.y) : uvec2(v.z, v.w);
}

#include "bismuth_kernel.sh"
#include "bismuth_gamestate.sh"

NUM_THREADS(CULL_THREADS, 1, 1)
void main()
{
    uint workGroupIndex = uint(gl_WorkGroupID.x);
    if (workGroupIndex >= BK_WORK_GROUP_COUNT)
        return;

    BkGameState gs = bkLoadGameState();

    uvec4 workGroup = b_sorted[BK_WORK_GROUP_OFFSET + workGroupIndex];
    uint outIndex = workGroup.z;

    for (uint i = workGroup.x; i < workGroup.y; i++) {
        uvec4 sorted = b_sorted[i];
        vec2 startPosition = vec2(uintBitsToFloat(sorted.x), uintBitsToFloat(sorted.y));
        uint groupIndex = sorted.z;
        uint spriteIndex = sorted.w;

        uint g = BK_GROUP_STATE_OFFSET + groupIndex * GROUP_STATE_STRIDE;
        BkGroupState state = bkDecodeGroupState(b_frame[g], b_frame[g + 1u], b_frame[g + 2u]);

        vec2 objectPosition = bkObjectPosition(state, startPosition);
        if (!bkIsOnScreen(gs, objectPosition))
            continue;

        uvec4 sprite = b_static[BK_INPUT_SPRITE_OFFSET + spriteIndex];
        uint o = sprite.x * OBJECT_INFO_STRIDE;
        BkObjectInfo object = bkDecodeObjectInfo(b_static[o], b_static[o + 1u], b_static[o + 2u]);

        BkSpriteResult result = bkProcessSprite(gs, state, object, sprite.y, objectPosition);
        if (!result.visible)
            continue;

        b_rawSprites[spriteIndex * RAW_SPRITE_STRIDE + 0u] = uvec4(
            floatBitsToUint(objectPosition.x),
            floatBitsToUint(objectPosition.y),
            bkPackColor(result.color),
            0u
        );
        b_rawSprites[spriteIndex * RAW_SPRITE_STRIDE + 1u] = floatBitsToUint(result.localTransform);

        uint triEnd = sprite.z + sprite.w;
        for (uint t = sprite.z; t < triEnd; t++) {
            b_culledTris[outIndex] = t;
            outIndex++;
        }
    }

    b_counters[workGroupIndex * WORK_GROUP_COUNTER_STRIDE] = outIndex - workGroup.z;
}
