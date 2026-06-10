#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 1, binding = 0) uniform sampler   u_samplers[];

layout(push_constant, std430) uniform Root
{
    uint  scene_tex;
    uint  bloom_tex;
    uint  smp;
    float strength;
} root;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main()
{
    vec3 scene = texture(sampler2D(u_textures[nonuniformEXT(root.scene_tex)], u_samplers[nonuniformEXT(root.smp)]), v_uv).rgb;
    vec3 bloom = texture(sampler2D(u_textures[nonuniformEXT(root.bloom_tex)], u_samplers[nonuniformEXT(root.smp)]), v_uv).rgb;
    vec3 hdr = scene + bloom * root.strength;
    vec3 tone = hdr / (hdr + vec3(1.0));
    o_color = vec4(pow(tone, vec3(1.0 / 2.2)), 1.0);
}
