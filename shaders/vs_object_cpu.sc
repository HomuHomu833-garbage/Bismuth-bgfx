$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0

/*
    CPU culling path: vertices are fully transformed and colored on the CPU.
*/
#include <bgfx_shader.sh>
#include "bismuth_clip.sh"

uniform mat4 u_bismuthMvp;

void main()
{
    v_texcoord0 = a_texcoord0;
    v_color0    = a_color0;
    gl_Position = bkFixClipSpace(mul(u_bismuthMvp, vec4(a_position.x, a_position.y, 0.0, 1.0)));
}
