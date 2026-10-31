$output v_texcoord0, v_color0

/*
    GPU-driven path: vertices are pulled from storage buffers using the
    compacted triangle list written by the culling compute shaders.
*/
#include <bgfx_compute.sh>
#include "bismuth_shared.sh"
#include "bismuth_clip.sh"

BUFFER_RO(b_vertices,    uvec4, 1);
BUFFER_RO(b_compactTris, uint,  2);
BUFFER_RO(b_rawSprites,  uvec4, 3);

uniform mat4 u_bismuthMvp;

void main()
{
    uint vertexId = uint(gl_VertexID);
    uint triIndex = b_compactTris[vertexId / 3u];
    uvec4 packedVertex = b_vertices[triIndex * 3u + vertexId % 3u];

    vec2 positionOffset = vec2(uintBitsToFloat(packedVertex.x), uintBitsToFloat(packedVertex.y));
    vec2 texCoord = vec2(float(packedVertex.z & 0xffffu), float(packedVertex.z >> 16u)) / 65535.0;
    uint spriteIndex = packedVertex.w;

    uvec4 rawA = b_rawSprites[spriteIndex * RAW_SPRITE_STRIDE + 0u];
    vec4  m    = uintBitsToFloat(b_rawSprites[spriteIndex * RAW_SPRITE_STRIDE + 1u]);

    vec2 position = vec2(uintBitsToFloat(rawA.x), uintBitsToFloat(rawA.y));
    position += vec2(m.x * positionOffset.x + m.z * positionOffset.y, m.y * positionOffset.x + m.w * positionOffset.y);

    uint c = rawA.z;
    v_color0 = vec4(
        float( c         & 0xffu),
        float((c >> 8u)  & 0xffu),
        float((c >> 16u) & 0xffu),
        float((c >> 24u) & 0xffu)
    ) / 255.0;

    v_texcoord0 = texCoord;
    gl_Position = bkFixClipSpace(mul(u_bismuthMvp, vec4(position.x, position.y, 0.0, 1.0)));
}
