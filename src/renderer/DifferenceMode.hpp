#pragma once

#include <common.hpp>
#include <gfx/HostGL.hpp>

class Renderer;

/*
    Debug view (F9): renders the scene once with vanilla rendering and once with
    Bismuth, and shows the amplified difference. Uses cocos2d render textures and
    a small GL program, so it works with every Bismuth backend.
*/
class DifferenceMode {
public:
    ~DifferenceMode();
    inline DifferenceMode(Renderer& renderer)
        : renderer(renderer) {}

    void drawSceneHook();

private:
    bool prepare(u32 width, u32 height);

    void destroy();

private:
    Renderer& renderer;

    float intensity = 5.0;
    float backdropIntensity = 0.2;

    cocos2d::CCSize lastSize = { -1.0, -1.0 };

    Ref<cocos2d::CCRenderTexture> vanillaTexture;
    Ref<cocos2d::CCRenderTexture> bismuthTexture;

    bismuth::gfx::GLuint program = 0;
    bismuth::gfx::GLuint quad = 0;
    bismuth::gfx::GLint locVanilla = -1, locBismuth = -1, locIntensity = -1, locBackdrop = -1;
};
