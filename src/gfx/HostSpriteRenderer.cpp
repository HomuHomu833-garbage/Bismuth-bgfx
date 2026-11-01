#include "HostSpriteRenderer.hpp"

namespace bismuth::gfx {

namespace {

constexpr u32 kVertexStride = 20; // CpuVertex: float x, y, u, v; u32 rgba8
constexpr GLuint kAttribPosition = 0;
constexpr GLuint kAttribTexCoord = 1;
constexpr GLuint kAttribColor    = 2;
constexpr GLuint kAttribCount    = 3;

const char* const kVertexShader = R"(
attribute vec2 a_position;
attribute vec2 a_texcoord0;
attribute vec4 a_color0;
uniform mat4 u_mvp;
varying vec2 v_texcoord0;
varying vec4 v_color0;
void main() {
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
    gl_Position = u_mvp * vec4(a_position, 0.0, 1.0);
}
)";

// Sprite sheets are up to 4096 texels wide: texture coordinates need highp.
const char* const kFragmentShader = R"(
#ifdef GL_ES
#  ifdef GL_FRAGMENT_PRECISION_HIGH
#    define TEXCOORD_PRECISION highp
#  else
#    define TEXCOORD_PRECISION mediump
#  endif
#else
#  define TEXCOORD_PRECISION
#endif
uniform sampler2D u_texture;
varying TEXCOORD_PRECISION vec2 v_texcoord0;
varying vec4 v_color0;
void main() {
    gl_FragColor = texture2D(u_texture, v_texcoord0) * v_color0;
}
)";

/// The GL state draw() changes, read before and put back after.
struct SavedState {
    GLint program = 0;
    GLint arrayBuffer = 0;
    GLint vertexArray = 0;
    GLint activeTexture = 0;
    GLint texture = 0;
    GLint sampler = 0;
    GLboolean blend = 0;
    GLboolean depthTest = 0;
    GLboolean cullFace = 0;
    GLint blendSrcRGB = 0, blendDstRGB = 0, blendSrcAlpha = 0, blendDstAlpha = 0;
    GLint blendEqRGB = 0, blendEqAlpha = 0;
    GLint attribEnabled[kAttribCount] = {};
};

} // namespace

bool HostSpriteRenderer::init(const HostGLInfo& info, bool allowVertexArray) {
    host = &info;
    auto& gl = glf();

    static const char* attributes[kAttribCount] = { "a_position", "a_texcoord0", "a_color0" };

    GLStateGuard guard(info);

    program = compileHostProgram(info, kVertexShader, kFragmentShader, attributes, kAttribCount, "");
    if (!program)
        return false;
    mvpLocation = gl.glGetUniformLocation(program, "u_mvp");
    textureLocation = gl.glGetUniformLocation(program, "u_texture");
    gl.glUseProgram(program);
    gl.glUniform1i(textureLocation, 0);

    gl.glGenBuffers(1, &vertexBuffer);

    if (allowVertexArray && info.hasVAO && gl.glGenVertexArrays && gl.glDeleteVertexArrays) {
        // The attribute layout lives in the VAO; the buffer is only ever
        // re-specified (glBufferData), so it stays valid.
        gl.glGenVertexArrays(1, &vertexArray);
        gl.glBindVertexArray(vertexArray);
        gl.glBindBuffer(gl::ARRAY_BUFFER, vertexBuffer);
        bindAttributes();
    }
    return true;
}

void HostSpriteRenderer::destroy(bool contextLost) {
    if (host && !contextLost) {
        auto& gl = glf();
        if (program)      gl.glDeleteProgram(program);
        if (vertexBuffer) gl.glDeleteBuffers(1, &vertexBuffer);
        if (vertexArray)  gl.glDeleteVertexArrays(1, &vertexArray);
    }
    *this = HostSpriteRenderer();
}

void HostSpriteRenderer::bindAttributes() {
    auto& gl = glf();
    gl.glEnableVertexAttribArray(kAttribPosition);
    gl.glEnableVertexAttribArray(kAttribTexCoord);
    gl.glEnableVertexAttribArray(kAttribColor);
    gl.glVertexAttribPointer(kAttribPosition, 2, gl::FLOAT, 0, kVertexStride, (const void*)0);
    gl.glVertexAttribPointer(kAttribTexCoord, 2, gl::FLOAT, 0, kVertexStride, (const void*)8);
    gl.glVertexAttribPointer(kAttribColor, 4, gl::UNSIGNED_BYTE, 1, kVertexStride, (const void*)16);
}

void HostSpriteRenderer::setVertices(const void* vertices, u32 vertexCount) {
    pendingVertices = vertices;
    pendingVertexCount = vertexCount;
    uploadPending = true;
}

void HostSpriteRenderer::draw(const float mvp[16], const Layer* layers, u32 count) {
    if (!program || count == 0)
        return;

    bool any = false;
    for (u32 i = 0; i < count; i++)
        any |= layers[i].vertexCount > 0 && layers[i].texture != 0;
    if (!any || (uploadPending && pendingVertexCount == 0))
        return;

    auto& gl = glf();
    const bool ownVAO = vertexArray != 0;

    // Save only what is changed below.
    SavedState s;
    gl.glGetIntegerv(gl::CURRENT_PROGRAM, &s.program);
    gl.glGetIntegerv(gl::ARRAY_BUFFER_BINDING, &s.arrayBuffer);
    if (ownVAO)
        gl.glGetIntegerv(gl::VERTEX_ARRAY_BINDING, &s.vertexArray);
    gl.glGetIntegerv(gl::ACTIVE_TEXTURE, &s.activeTexture);
    if (s.activeTexture != (GLint)gl::TEXTURE0)
        gl.glActiveTexture(gl::TEXTURE0);
    gl.glGetIntegerv(gl::TEXTURE_BINDING_2D, &s.texture);
    if (host->hasSamplerObjects)
        gl.glGetIntegerv(gl::SAMPLER_BINDING, &s.sampler);
    s.blend = gl.glIsEnabled(gl::BLEND);
    s.depthTest = gl.glIsEnabled(gl::DEPTH_TEST); // both off in GD, but must be for sprites
    s.cullFace = gl.glIsEnabled(gl::CULL_FACE);
    gl.glGetIntegerv(gl::BLEND_SRC_RGB, &s.blendSrcRGB);
    gl.glGetIntegerv(gl::BLEND_DST_RGB, &s.blendDstRGB);
    gl.glGetIntegerv(gl::BLEND_SRC_ALPHA, &s.blendSrcAlpha);
    gl.glGetIntegerv(gl::BLEND_DST_ALPHA, &s.blendDstAlpha);
    gl.glGetIntegerv(gl::BLEND_EQUATION_RGB, &s.blendEqRGB);
    gl.glGetIntegerv(gl::BLEND_EQUATION_ALPHA, &s.blendEqAlpha);
    if (!ownVAO) {
        for (GLuint i = 0; i < kAttribCount; i++)
            gl.glGetVertexAttribiv(i, gl::VERTEX_ATTRIB_ARRAY_ENABLED, &s.attribEnabled[i]);
    }

    // Draw.
    gl.glUseProgram(program);
    gl.glUniformMatrix4fv(mvpLocation, 1, 0, mvp);

    if (ownVAO)
        gl.glBindVertexArray(vertexArray);
    gl.glBindBuffer(gl::ARRAY_BUFFER, vertexBuffer);
    if (uploadPending) {
        // Re-specifying the whole buffer lets the driver use new storage
        // instead of waiting for the GPU to finish with the previous frame's.
        gl.glBufferData(gl::ARRAY_BUFFER, (GLsizeiptr)pendingVertexCount * kVertexStride, pendingVertices, gl::STREAM_DRAW);
        uploadPending = false;
        pendingVertices = nullptr;
    }
    if (!ownVAO)
        bindAttributes(); // cocos2d re-specifies its attribute pointers before each draw

    if (host->hasSamplerObjects && s.sampler != 0)
        gl.glBindSampler(0, 0);

    if (s.depthTest) gl.glDisable(gl::DEPTH_TEST);
    if (s.cullFace)  gl.glDisable(gl::CULL_FACE);
    gl.glEnable(gl::BLEND);
    gl.glBlendEquationSeparate(gl::FUNC_ADD, gl::FUNC_ADD);

    GLuint boundTexture = 0;
    int boundBlend = -1;
    for (u32 i = 0; i < count; i++) {
        const Layer& layer = layers[i];
        if (layer.vertexCount == 0 || layer.texture == 0)
            continue;

        if (layer.texture != boundTexture) {
            gl.glBindTexture(gl::TEXTURE_2D, layer.texture);
            boundTexture = layer.texture;
        }
        if ((int)layer.additive != boundBlend) {
            if (layer.additive)
                gl.glBlendFuncSeparate(gl::SRC_ALPHA, gl::ONE, gl::SRC_ALPHA, gl::ONE);
            else
                gl.glBlendFuncSeparate(gl::ONE, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
            boundBlend = (int)layer.additive;
        }
        gl.glDrawArrays(gl::TRIANGLES, (GLint)layer.firstVertex, (GLsizei)layer.vertexCount);
    }

    // Restore.
    if (ownVAO) {
        gl.glBindVertexArray((GLuint)s.vertexArray);
    } else {
        for (GLuint i = 0; i < kAttribCount; i++) {
            if (s.attribEnabled[i]) gl.glEnableVertexAttribArray(i);
            else                    gl.glDisableVertexAttribArray(i);
        }
    }
    gl.glBindBuffer(gl::ARRAY_BUFFER, (GLuint)s.arrayBuffer);
    gl.glUseProgram((GLuint)s.program);

    gl.glBindTexture(gl::TEXTURE_2D, (GLuint)s.texture);
    if (host->hasSamplerObjects && s.sampler != 0)
        gl.glBindSampler(0, (GLuint)s.sampler);
    if (s.activeTexture != (GLint)gl::TEXTURE0)
        gl.glActiveTexture((GLenum)s.activeTexture);

    if (s.depthTest) gl.glEnable(gl::DEPTH_TEST);
    if (s.cullFace)  gl.glEnable(gl::CULL_FACE);
    if (!s.blend)
        gl.glDisable(gl::BLEND);
    gl.glBlendFuncSeparate((GLenum)s.blendSrcRGB, (GLenum)s.blendDstRGB, (GLenum)s.blendSrcAlpha, (GLenum)s.blendDstAlpha);
    gl.glBlendEquationSeparate((GLenum)s.blendEqRGB, (GLenum)s.blendEqAlpha);
}

} // namespace bismuth::gfx
