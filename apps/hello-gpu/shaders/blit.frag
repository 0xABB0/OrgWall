#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 1, binding = 0) uniform sampler   u_samplers[];

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
