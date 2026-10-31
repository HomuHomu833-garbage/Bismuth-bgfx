#include "common.hpp"
#include <gfx/Device.hpp>

using namespace geode::prelude;

std::chrono::steady_clock::time_point modStartTime;

$on_mod(Loaded) {
    modStartTime = std::chrono::high_resolution_clock::now();
}

std::string byteSizeToString(usize size) {
    std::string unit = "B";
    double dsize = size;
    if (dsize > 1000.0) { dsize /= 1000.0; unit = "kB"; }
    if (dsize > 1000.0) { dsize /= 1000.0; unit = "MB"; }
    return fmt::format("{:.3f}{}", dsize, unit);
}

static std::string currentGLVendor() {
    auto& host = bismuth::gfx::Device::get().getHost();
    if (!host.vendor.empty())
        return host.vendor;
    bismuth::gfx::HostGLInfo info;
    bismuth::gfx::queryHostGL(info);
    return info.vendor;
}

bool isNVidiaGPU() { return currentGLVendor().starts_with("NVIDIA"); }
bool isAMDGPU()    { auto v = currentGLVendor(); return v.starts_with("AMD") || v.starts_with("ATI"); }
bool isIntelGPU()  { return currentGLVendor().starts_with("Intel"); }

namespace bismuth {

void logMessage(LogLevel level, const char* message) {
    switch (level) {
    case LogLevel::Debug: log::debug("{}", message); break;
    case LogLevel::Info:  log::info("{}", message); break;
    case LogLevel::Warn:  log::warn("{}", message); break;
    case LogLevel::Error: log::error("{}", message); break;
    }
}

} // namespace bismuth

std::optional<std::string> readResourceFile(const fs::path& path) {
    std::ifstream t(Mod::get()->getResourcesDir() / path);
    if (!t.is_open())
        return std::nullopt;

    t.seekg(0, std::ios::end);
    size_t size = t.tellg();
    std::string buffer(size, ' ');
    t.seekg(0);
    t.read(buffer.data(), size);
    t.close();

    return buffer;
}