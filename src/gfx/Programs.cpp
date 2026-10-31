#include "Programs.hpp"
#include "shaders/generated/shaders.hpp"

#include <cstring>

namespace bismuth::gfx {

ShaderProfile shaderProfileFor(bgfx::RendererType::Enum renderer, bool gpuPath) {
    switch (renderer) {
    case bgfx::RendererType::Vulkan:   return ShaderProfile::Spirv;
    case bgfx::RendererType::Metal:    return ShaderProfile::Metal;
    case bgfx::RendererType::OpenGLES: return ShaderProfile::Essl100;
    default:                           return gpuPath ? ShaderProfile::Glsl430 : ShaderProfile::Glsl120;
    }
}

bgfx::ShaderHandle loadShader(const char* name, ShaderProfile profile) {
    for (const auto& shader : shaders::kShaders) {
        if (std::strcmp(shader.name, name) != 0)
            continue;

        const uint8_t* data = nullptr;
        size_t size = 0;
        switch (profile) {
        case ShaderProfile::Spirv:   data = shader.spirv;   size = shader.spirvSize;   break;
        case ShaderProfile::Metal:   data = shader.metal;   size = shader.metalSize;   break;
        case ShaderProfile::Glsl430: data = shader.glsl430; size = shader.glsl430Size; break;
        case ShaderProfile::Glsl120: data = shader.glsl120; size = shader.glsl120Size; break;
        case ShaderProfile::Essl100: data = shader.essl100; size = shader.essl100Size; break;
        }

        if (!data) {
            logf(LogLevel::Error, "Bismuth: shader %s isn't available for profile %d", name, (int)profile);
            return BGFX_INVALID_HANDLE;
        }

        auto handle = bgfx::createShader(bgfx::makeRef(data, (u32)size));
        bgfx::setName(handle, name);
        return handle;
    }

    logf(LogLevel::Error, "Bismuth: unknown shader %s", name);
    return BGFX_INVALID_HANDLE;
}

bgfx::ProgramHandle loadProgram(const char* vs, const char* fs, ShaderProfile profile) {
    auto vsh = loadShader(vs, profile);
    auto fsh = loadShader(fs, profile);
    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
        if (bgfx::isValid(vsh)) bgfx::destroy(vsh);
        if (bgfx::isValid(fsh)) bgfx::destroy(fsh);
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(vsh, fsh, true);
}

bgfx::ProgramHandle loadComputeProgram(const char* cs, ShaderProfile profile) {
    auto csh = loadShader(cs, profile);
    if (!bgfx::isValid(csh))
        return BGFX_INVALID_HANDLE;
    return bgfx::createProgram(csh, true);
}

} // namespace bismuth::gfx
