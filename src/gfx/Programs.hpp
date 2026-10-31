#pragma once

#include <core/Types.hpp>
#include <bgfx/bgfx.h>

namespace bismuth::gfx {

enum class ShaderProfile { Spirv, Metal, Glsl430, Glsl120, Essl100 };

/// Profile for the active renderer. `gpuPath` selects GLSL 4.30 on desktop GL
/// (compute + storage buffers); otherwise GLSL 1.20 / ESSL 1.00.
ShaderProfile shaderProfileFor(bgfx::RendererType::Enum renderer, bool gpuPath);

/// Creates a shader from the embedded binaries (src/gfx/shaders/generated).
bgfx::ShaderHandle loadShader(const char* name, ShaderProfile profile);

bgfx::ProgramHandle loadProgram(const char* vs, const char* fs, ShaderProfile profile);
bgfx::ProgramHandle loadComputeProgram(const char* cs, ShaderProfile profile);

} // namespace bismuth::gfx
