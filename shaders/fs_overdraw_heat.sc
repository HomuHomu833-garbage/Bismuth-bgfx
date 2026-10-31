$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_overdraw, 0);

vec3 bkHeatColor(float v)
{
    if (v <= 3.0)
        return mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), v / 3.0);
    if (v <= 7.0)
        return mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (v - 3.0) / 4.0);
    if (v <= 12.0)
        return mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (v - 7.0) / 5.0);
    return mix(vec3(1.0, 0.0, 0.0), vec3(0.8, 0.0, 0.0), min((v - 12.0) / 5.0, 1.0));
}

void main()
{
    float count = texture2D(s_overdraw, v_texcoord0).x * 255.0;
    // Premultiplied alpha, composited like every other Bismuth layer.
    gl_FragColor = vec4(bkHeatColor(count) * 0.5, 0.5);
}
