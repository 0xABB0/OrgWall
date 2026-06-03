#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 1) uniform sampler   u_samplers[];

layout(push_constant, std430) uniform Root
{
    uint  tex;
    uint  smp;
    float time;
    float pad;
} root;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

vec3 palette(float t)
{
    vec3 a = vec3(0.5, 0.5, 0.5);
    vec3 b = vec3(0.5, 0.5, 0.5);
    vec3 c = vec3(1.0, 1.0, 0.9);
    vec3 d = vec3(0.0, 0.15, 0.40);
    return a + b * cos(6.28318530 * (c * t + d));
}

void main()
{
    vec2  ab = texture(sampler2D(u_textures[nonuniformEXT(root.tex)], u_samplers[nonuniformEXT(root.smp)]), v_uv).xy;
    float t = ab.x - ab.y;
    t = clamp(t * 0.5 + 0.5, 0.0, 1.0);
    o_color = vec4(palette(t), 1.0);
}
