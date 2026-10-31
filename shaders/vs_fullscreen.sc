$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

void main()
{
    gl_Position = vec4(a_position.x, a_position.y, 0.5, 1.0);
    v_texcoord0 = a_position * 0.5 + 0.5;
}
