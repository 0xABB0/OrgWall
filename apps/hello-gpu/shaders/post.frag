#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 1, binding = 0) uniform sampler   u_samplers[];

layout(push_constant, std430) uniform Root
{
    uint  tex;
    uint  smp;
    float amount;
    float vignette;
} root;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

vec3 sample_rgb(vec2 uv)
{
    return texture(sampler2D(u_textures[nonuniformEXT(root.tex)], u_samplers[nonuniformEXT(root.smp)]), uv).rgb;
}

void main()
{
    vec2 centered = v_uv - 0.5;
    vec2 dir = centered * (root.amount * dot(centered, centered) * 4.0);
    vec3 col;
    col.r = sample_rgb(v_uv + dir).r;
    col.g = sample_rgb(v_uv).g;
    col.b = sample_rgb(v_uv - dir).b;

    float r = length(centered) * 1.41421356;
    float vig = clamp(1.0 - root.vignette * r * r, 0.0, 1.0);
    col *= vig;

    col = pow(col, vec3(0.9)) * 1.05 + 0.02;
    o_color = vec4(col, 1.0);
}
