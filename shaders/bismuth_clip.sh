/*
    Cocos2d supplies OpenGL-style projection matrices (clip space z in [-w, w]).
    Vulkan and Metal clip z to [0, w], so remap there. Y is left alone: the
    compositor accounts for each backend's framebuffer orientation.
*/
vec4 bkFixClipSpace(vec4 position)
{
#if BGFX_SHADER_LANGUAGE_SPIRV || BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_HLSL
    position.z = (position.z + position.w) * 0.5;
#endif
    return position;
}
