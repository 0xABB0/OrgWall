#version 460
#extension GL_EXT_nonuniform_qualifier : require

// Side-by-side compositor for the MSAA screen: the left half samples the
// multisample-resolved target, the right half the single-sample reference, both
// through the bindless heap. A hairline seam marks the split so the smoother
// resolved edges read against the stair-stepped reference at a glance.
layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 1) uniform sampler   u_samplers[];

layout(push_constant, std430) uniform Root
{
    uint resolved; // bindless slot of the MSAA-resolved target (left)
    uint reference; // bindless slot of the single-sample target (right)
    uint smp;
    uint pad;
} root;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main()
{
    bool left = v_uv.x < 0.5;
    // Each half samples its own target across [0,1] so the same star fills both.
    vec2 uv = vec2(left ? v_uv.x * 2.0 : (v_uv.x - 0.5) * 2.0, v_uv.y);
    uint slot = left ? root.resolved : root.reference;
    vec4 c = texture(sampler2D(u_textures[nonuniformEXT(slot)], u_samplers[nonuniformEXT(root.smp)]), uv);

    float seam = smoothstep(0.0015, 0.0, abs(v_uv.x - 0.5));
    o_color = mix(c, vec4(0.0, 0.0, 0.0, 1.0), seam);
}
