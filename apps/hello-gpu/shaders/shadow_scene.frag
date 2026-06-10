#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 1, binding = 0) uniform sampler   u_samplers[];

layout(push_constant, std430) uniform Root
{
    uint  shadow_tex;
    uint  shadow_smp;
    uint  pad0;
    uint  pad1;
    vec3  light_dir;
    float pad2;
} root;

layout(location = 0) in  vec3 v_normal;
layout(location = 1) in  vec3 v_color;
layout(location = 2) in  vec3 v_shadow_uvz;

layout(location = 0) out vec4 o_color;

void main()
{
    vec3  n     = normalize(v_normal);
    vec3  ld    = normalize(-root.light_dir);
    float ndotl = max(dot(n, ld), 0.0);

    vec2  suv = v_shadow_uvz.xy;
    float sz  = v_shadow_uvz.z;
    float sd  = texture(sampler2D(u_textures[nonuniformEXT(root.shadow_tex)], u_samplers[nonuniformEXT(root.shadow_smp)]), suv).r;
    float lit = (sz - 0.003 <= sd) ? 1.0 : 0.2;

    vec3 col = v_color * (0.15 + 0.85 * ndotl * lit);
    o_color  = vec4(col, 1.0);
}
