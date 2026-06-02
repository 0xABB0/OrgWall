#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 1) uniform sampler   u_samplers[];

layout(push_constant, std430) uniform Root
{
    uint resolved;
    uint reference;
    uint smp;
    uint pad;
} root;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main()
{
    bool left = v_uv.x < 0.5;
    vec2 uv = vec2(left ? v_uv.x * 2.0 : (v_uv.x - 0.5) * 2.0, v_uv.y);
    uint slot = left ? root.resolved : root.reference;
    vec4 c = texture(sampler2D(u_textures[nonuniformEXT(slot)], u_samplers[nonuniformEXT(root.smp)]), uv);

    float seam = smoothstep(0.0015, 0.0, abs(v_uv.x - 0.5));
    o_color = mix(c, vec4(0.0, 0.0, 0.0, 1.0), seam);
}
