$input v_texcoord0, v_color0

/*
    Overdraw view: every rasterized fragment adds 1/255 to the counter target
    (additive blending), mirroring the original stencil-increment approach.
*/
#include <bgfx_shader.sh>

void main()
{
    gl_FragColor = vec4(1.0 / 255.0, 0.0, 0.0, 0.0);
}
