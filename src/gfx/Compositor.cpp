#include "Interop.hpp"

#if defined(__APPLE__)
#   include <TargetConditionals.h>
#endif

namespace bismuth::gfx {

void Interop::registerTarget(SharedTarget* target) {
    for (auto& slot : targets) {
        if (!slot) {
            slot = target;
            return;
        }
    }
}

void Interop::unregisterTarget(SharedTarget* target) {
    for (auto& slot : targets) {
        if (slot == target)
            slot = nullptr;
    }
}

static const char* kVertexShader = R"(
attribute vec2 a_position;
uniform vec2 u_uvScale;
uniform float u_flipY;
varying vec2 v_uv;
void main() {
    vec2 uv = a_position * 0.5 + 0.5;
    uv.y = mix(uv.y, 1.0 - uv.y, u_flipY);
    v_uv = uv * u_uvScale;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

static const char* kFragmentShader2D = R"(
uniform sampler2D u_texture;
varying vec2 v_uv;
void main() {
    gl_FragColor = texture2D(u_texture, v_uv);
}
)";

static const char* kFragmentShaderRect = R"(
uniform sampler2DRect u_texture;
varying vec2 v_uv;
void main() {
    gl_FragColor = texture2DRect(u_texture, v_uv);
}
)";

Compositor::~Compositor() {
    destroy();
}

bool Compositor::init(const HostGLInfo& info) {
    host = &info;
    auto& gl = glf();

    static const char* attributes[] = { "a_position" };

    auto link = [&](Program& p, const char* fs, const char* prologue) {
        p.program = compileHostProgram(info, kVertexShader, fs, attributes, 1, prologue);
        if (!p.program)
            return false;
        p.texture = gl.glGetUniformLocation(p.program, "u_texture");
        p.uvScale = gl.glGetUniformLocation(p.program, "u_uvScale");
        p.flipY   = gl.glGetUniformLocation(p.program, "u_flipY");
        return true;
    };

    GLStateGuard guard(info);

    if (!link(program2D, kFragmentShader2D, ""))
        return false;

#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    if (!info.isES && !link(programRect, kFragmentShaderRect, "#extension GL_ARB_texture_rectangle : enable"))
        return false;
#endif

    static const float quad[] = { -1.f, -1.f,  1.f, -1.f,  -1.f, 1.f,  1.f, 1.f };
    gl.glGenBuffers(1, &vertexBuffer);
    gl.glBindBuffer(gl::ARRAY_BUFFER, vertexBuffer);
    gl.glBufferData(gl::ARRAY_BUFFER, sizeof(quad), quad, gl::STATIC_DRAW);
    return true;
}

void Compositor::destroy() {
    if (!host)
        return;
    auto& gl = glf();
    if (program2D.program)   gl.glDeleteProgram(program2D.program);
    if (programRect.program) gl.glDeleteProgram(programRect.program);
    if (vertexBuffer)        gl.glDeleteBuffers(1, &vertexBuffer);
    program2D = {};
    programRect = {};
    vertexBuffer = 0;
    host = nullptr;
}

void Compositor::draw(const SharedTarget& target) {
    if (!host || !target.isValid())
        return;

    const bool rect = target.glTarget == gl::TEXTURE_RECTANGLE;
    const Program& p = rect ? programRect : program2D;
    if (!p.program)
        return;

    auto& gl = glf();
    GLStateGuard guard(*host);

    if (host->hasVAO)
        gl.glBindVertexArray(0);

    gl.glUseProgram(p.program);
    gl.glUniform1i(p.texture, 0);
    if (rect) gl.glUniform2f(p.uvScale, (float)target.width, (float)target.height);
    else      gl.glUniform2f(p.uvScale, 1.f, 1.f);
    gl.glUniform1f(p.flipY, target.flipY ? 1.f : 0.f);

    gl.glActiveTexture(gl::TEXTURE0);
    gl.glBindTexture(target.glTarget, target.glTexture);
    if (host->hasSamplerObjects)
        gl.glBindSampler(0, 0);

    gl.glDisable(gl::DEPTH_TEST);
    gl.glDisable(gl::CULL_FACE);
    gl.glDisable(gl::STENCIL_TEST);
    gl.glColorMask(1, 1, 1, 1);
    gl.glEnable(gl::BLEND);
    gl.glBlendEquationSeparate(gl::FUNC_ADD, gl::FUNC_ADD);
    gl.glBlendFuncSeparate(gl::ONE, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);

    gl.glBindBuffer(gl::ARRAY_BUFFER, vertexBuffer);
    for (GLuint i = 1; i < 8; i++)
        gl.glDisableVertexAttribArray(i);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(0, 2, gl::FLOAT, 0, 0, nullptr);

    gl.glDrawArrays(gl::TRIANGLE_STRIP, 0, 4);

    if (rect)
        gl.glBindTexture(target.glTarget, 0);
}

} // namespace bismuth::gfx
