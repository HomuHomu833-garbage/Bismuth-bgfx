/*
    CPU-only checks of the culling kernel and the CPU culler.
*/
#include "SyntheticScene.hpp"
#include <renderer/CpuCuller.hpp>
#include <cstdio>
#include <cstdlib>

namespace bismuth {
void logMessage(LogLevel, const char* message) { std::fprintf(stderr, "%s\n", message); }
}

using namespace bismuth;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main() {
    // A scene where every sprite of group 1 pulses with base-only and detail-only
    // entries: with the original shader's loop this never terminated.
    auto scene = test::makeScene(320, 240, 600, 1234);

    CpuCuller culler(4);
    std::vector<CpuVertex> vertices;
    std::vector<LayerVertexRange> layers;
    culler.run(scene.staticData, scene.sorted, scene.frame, scene.gameState, vertices, layers);

    CHECK(layers.size() == scene.layerCount);

    u32 total = 0;
    for (auto& layer : layers) {
        CHECK(layer.firstVertex == total);
        CHECK(layer.vertexCount % 3 == 0);
        total += layer.vertexCount;
    }
    CHECK(total == vertices.size());
    CHECK(total > 0);
    CHECK(total < scene.staticData.triangleCount * 3); // some sprites must be culled

    // Same result with a different thread count and work group size.
    auto scene2 = test::makeScene(320, 240, 600, 1234, 61);
    CpuCuller culler1(1);
    std::vector<CpuVertex> vertices2;
    std::vector<LayerVertexRange> layers2;
    culler1.run(scene2.staticData, scene2.sorted, scene2.frame, scene2.gameState, vertices2, layers2);
    CHECK(vertices2.size() == vertices.size());
    CHECK(std::memcmp(vertices2.data(), vertices.data(), vertices.size() * sizeof(CpuVertex)) == 0);

    // Every emitted vertex has a premultiplied color (rgb <= a).
    for (auto& v : vertices) {
        u32 r = v.color & 0xff, g = (v.color >> 8) & 0xff, b = (v.color >> 16) & 0xff, a = v.color >> 24;
        if (r > a + 1 || g > a + 1 || b > a + 1) {
            CHECK(!"color not premultiplied");
            break;
        }
    }

    std::printf("kernel_test: %zu visible vertices of %u, %s\n", vertices.size(), scene.staticData.triangleCount * 3,
                failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
