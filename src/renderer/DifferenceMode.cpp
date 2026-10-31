#include "Renderer.hpp"
#include <gfx/Device.hpp>

using namespace geode::prelude;
namespace gfx = bismuth::gfx;

static const char* kVertexShader = R"(
attribute vec2 a_position;
varying vec2 v_uv;
void main() {
    v_uv = a_position * 0.5 + 0.5;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

static const char* kFragmentShader = R"(
uniform sampler2D u_vanillaFrame;
uniform sampler2D u_bismuthFrame;
uniform float u_intensity;
uniform float u_backdropIntensity;
varying vec2 v_uv;
void main() {
    vec3 vcol = texture2D(u_vanillaFrame, v_uv).rgb;
    vec3 bcol = texture2D(u_bismuthFrame, v_uv).rgb;
    vec3 diff = abs(vcol - bcol) * u_intensity + vcol * u_backdropIntensity;
    gl_FragColor = vec4(diff, 1.0);
}
)";

DifferenceMode::~DifferenceMode() {
    destroy();
}

void DifferenceMode::destroy() {
    auto& device = gfx::Device::get();
    if ((program || quad) && device.isInitialized() && !device.isHostContextLost()) {
        auto& gl = gfx::glf();
        if (program) gl.glDeleteProgram(program);
        if (quad) gl.glDeleteBuffers(1, &quad);
    }
    program = 0;
    quad = 0;
    vanillaTexture = nullptr;
    bismuthTexture = nullptr;
}

bool DifferenceMode::prepare(u32 width, u32 height) {
    vanillaTexture = CCRenderTexture::create(width, height);
    bismuthTexture = CCRenderTexture::create(width, height);

    if (!program) {
        auto& host = gfx::Device::get().getHost();
        auto& gl = gfx::glf();
        gfx::GLStateGuard guard(host);

        static const char* attributes[] = { "a_position" };
        program = gfx::compileHostProgram(host, kVertexShader, kFragmentShader, attributes, 1);
        if (!program)
            return false;
        locVanilla   = gl.glGetUniformLocation(program, "u_vanillaFrame");
        locBismuth   = gl.glGetUniformLocation(program, "u_bismuthFrame");
        locIntensity = gl.glGetUniformLocation(program, "u_intensity");
        locBackdrop  = gl.glGetUniformLocation(program, "u_backdropIntensity");

        static const float positions[] = { -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f };
        gl.glGenBuffers(1, &quad);
        gl.glBindBuffer(gfx::gl::ARRAY_BUFFER, quad);
        gl.glBufferData(gfx::gl::ARRAY_BUFFER, sizeof(positions), positions, gfx::gl::STATIC_DRAW);
    }
    return vanillaTexture && bismuthTexture;
}

void DifferenceMode::drawSceneHook() {
    auto director = CCDirector::get();
    auto currentSize = director->getWinSize();

    if (lastSize != currentSize) {
        if (!prepare((u32)currentSize.width, (u32)currentSize.height))
            return;
        lastSize = currentSize;
    }

    bool prevEnabled = renderer.isEnabled();

    // The two renders of the same frame.
    renderer.setEnabled(false);
    vanillaTexture->beginWithClear(0, 0, 0, 1);
    director->getRunningScene()->visit();
    vanillaTexture->end();

    renderer.setEnabled(true);
    bismuthTexture->beginWithClear(0, 0, 0, 1);
    director->getRunningScene()->visit();
    bismuthTexture->end();

    renderer.setEnabled(prevEnabled);

    auto& host = gfx::Device::get().getHost();
    auto& gl = gfx::glf();
    {
        gfx::GLStateGuard guard(host);

        gl.glClear(gfx::gl::COLOR_BUFFER_BIT);

        if (host.hasVAO)
            gl.glBindVertexArray(0);
        gl.glUseProgram(program);
        gl.glUniform1i(locVanilla, 0);
        gl.glUniform1i(locBismuth, 1);
        gl.glUniform1f(locIntensity, intensity);
        gl.glUniform1f(locBackdrop, backdropIntensity);

        gl.glActiveTexture(gfx::gl::TEXTURE0);
        gl.glBindTexture(gfx::gl::TEXTURE_2D, vanillaTexture->getSprite()->getTexture()->getName());
        gl.glActiveTexture(gfx::gl::TEXTURE0 + 1);
        gl.glBindTexture(gfx::gl::TEXTURE_2D, bismuthTexture->getSprite()->getTexture()->getName());

        gl.glDisable(gfx::gl::BLEND);
        gl.glBindBuffer(gfx::gl::ARRAY_BUFFER, quad);
        for (gfx::GLuint i = 1; i < 8; i++)
            gl.glDisableVertexAttribArray(i);
        gl.glEnableVertexAttribArray(0);
        gl.glVertexAttribPointer(0, 2, gfx::gl::FLOAT, 0, 0, nullptr);
        gl.glDrawArrays(gfx::gl::TRIANGLE_STRIP, 0, 4);
    }

    director->getOpenGLView()->swapBuffers();
}

#include <Geode/modify/CCDirector.hpp>
class $modify(DifferenceModeCCDirector, CCDirector) {
    void drawScene() {
        DifferenceMode* diffMode = nullptr;
        auto renderer = Renderer::get();
        if (renderer && renderer->isActive())
            diffMode = renderer->getDifferenceMode();

        if (!diffMode) {
            CCDirector::drawScene();
            return;
        }

        calculateDeltaTime();
        if (!m_bPaused)
            m_pScheduler->update(m_fDeltaTime);
        if (m_pNextScene)
            setNextScene();
        m_uTotalFrames++;

        diffMode->drawSceneHook();
    }
};
