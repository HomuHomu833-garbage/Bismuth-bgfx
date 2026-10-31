#pragma once

/*
    Plain data exchanged between the Geometry Dash side of the renderer and the
    GD-independent batch renderer (GPU culling or CPU culling). Every array here
    is uploaded as-is to the GPU, see shaders/bismuth_shared.sh for the layouts.
*/

#include <core/Types.hpp>
#include <vector>
#include "../../shaders/bismuth_shared.sh"

namespace bismuth {

/// One vertex of a sprite mesh, relative to the owning object's position.
struct PackedVertex {
    float positionOffsetX;
    float positionOffsetY;
    u32   texCoord;     // unorm16 x2 (u in low bits)
    u32   spriteIndex;  // index into the input sprites
};
static_assert(sizeof(PackedVertex) == sizeof(UVec4));

inline u32 packTexCoord(float u, float v) {
    auto q = [](float x) -> u32 {
        x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
        return (u32)(x * 65535.f + 0.5f);
    };
    return q(u) | (q(v) << 16);
}

inline glm::vec2 unpackTexCoord(u32 packed) {
    return glm::vec2(float(packed & 0xffffu), float(packed >> 16)) / 65535.f;
}

/// Data that never changes during a level.
struct StaticBatchData {
    std::vector<UVec4> staticBuffer;    // object infos, then input sprites
    u32 inputSpriteOffset = 0;
    u32 inputSpriteCount = 0;
    std::vector<PackedVertex> vertices; // 3 per triangle
    u32 triangleCount = 0;
};

/// Sprites sorted for one combination of color channel blending states.
struct SortedBatchData {
    std::vector<UVec4> buffer;          // sorted sprites, then work groups, then layers
    u32 workGroupOffset = 0;
    u32 workGroupCount = 0;
    u32 layerOffset = 0;
    u32 layerCount = 0;
};

/// Game state for one frame (mirrors u_gameState, see bismuth_shared.sh).
struct GameStateUniforms {
    float mvp[16] = {};
    float timer = 0.f;
    float audioScale = 0.f;
    glm::vec2 cameraViewSize {};
    glm::vec2 cameraPosition {};
    glm::vec2 cameraRight {};
    glm::vec2 cameraUp {};
    glm::vec2 cameraSize {};
    glm::vec2 winSize {};
    float screenRight = 0.f;
    float cameraUnzoomedX = 0.f;
    glm::vec3 specialLightBGColor {};
    u32 flags = 0;
};

/// Per-frame buffer: color channels, group states and pulse entries.
struct FrameBatchData {
    std::vector<UVec4> buffer;
    u32 groupStateOffset = 0;
    u32 pulseOffset = 0;
};

} // namespace bismuth
