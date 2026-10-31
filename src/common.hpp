#pragma once

#include <core/Types.hpp>
#include <Geode/Geode.hpp>
#include <glm/glm.hpp>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdint.h>

// Constants shared with the shaders (color channels, object flags, ...).
#include "../shaders/bismuth_shared.sh"

#define MOVE_TRIGGER_ID   901
#define ALPHA_TRIGGER_ID  1007
#define TOGGLE_TRIGGER_ID 1049
#define ROTATE_TRIGGER_ID 1346
#define FOLLOW_TRIGGER_ID 1347
#define GUIDE_OBJECT_ID   2016

#ifdef __GNUC__
#define PACKED( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

#ifdef _MSC_VER
#define PACKED( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#endif

//#define DEBUG_LOG(...) log::info(__VA_ARGS__)
#define DEBUG_LOG(...)

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;

namespace fs = std::filesystem;

// #define BISMUTH_ASSERT(COND) { if (!(COND)) geode::log::error("{}:{}: Assertion failed: {}", fs::path(__FILE__).filename(), __LINE__, #COND); }
#define BISMUTH_ASSERT(COND)

inline vec2 ccPointToGLM(const cocos2d::CCPoint& point) {
    return vec2(point.x, point.y);
}

inline vec3 ccColor3BToGLM(const cocos2d::ccColor3B& color) {
    return vec3(color.r / 255.f, color.g / 255.f, color.b / 255.f);
}

extern std::chrono::steady_clock::time_point modStartTime;

inline u64 getTime() {
    return (std::chrono::high_resolution_clock::now() - modStartTime).count();
}

std::string byteSizeToString(usize size);

/// GPU vendor of Geometry Dash's GL context (GL_VENDOR string).
bool isNVidiaGPU();
bool isAMDGPU();
bool isIntelGPU();

std::optional<std::string> readResourceFile(const fs::path& path);

inline glm::vec2 getClockwise(const glm::vec2& v) {
    return { v.y, -v.x };
}

inline glm::vec2 getCounterClockwise(const glm::vec2& v) {
    return { -v.y, v.x };
}