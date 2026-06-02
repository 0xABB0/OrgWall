#version 460

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main()
{
    vec3 top = vec3(0.05, 0.07, 0.16);
    vec3 bottom = vec3(0.18, 0.10, 0.22);
    vec3 c = mix(top, bottom, v_uv.y);
    c += 0.05 * vec3(v_uv.x, 0.5 * v_uv.x, 1.0 - v_uv.x);
    o_color = vec4(c, 1.0);
}
