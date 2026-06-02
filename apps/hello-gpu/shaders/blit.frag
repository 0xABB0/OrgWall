#version 460
#extension GL_EXT_nonuniform_qualifier : require

// Bindless blit: sample one heap-resident texture with one heap-resident
// sampler, both addressed by index from the push-constant root record. The
// set-0 runtime arrays are the heap signature reflection keys on to mark the
// pipeline bindless (gpu-rhi.md §6.7).
layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 1) uniform sampler   u_samplers[];

layout(push_constant, std430) uniform Root
{
    uint tex;
    uint smp;
} root;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main()
{
    o_color = texture(sampler2D(u_textures[nonuniformEXT(root.tex)], u_samplers[nonuniformEXT(root.smp)]), v_uv);
}
