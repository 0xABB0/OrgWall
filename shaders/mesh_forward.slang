struct Mesh3DVertex
{
    float4 position;
    float4 normal;
    float4 color;
    float2 uv;
    float2 _pad;
    float4 tangent;
};

struct MaterialParams
{
    float4 base_color;
    uint base_color_texture_idx;
    uint normal_texture_idx;
    uint metallic_roughness_texture_idx;
    uint flags;
    float alpha_cutoff;
    float normal_scale;
    float metallic_factor;
    float roughness_factor;
};

static const uint MATERIAL_FLAG_USE_BASE_COLOR_TEXTURE = 1 << 0;
static const uint MATERIAL_FLAG_ALPHA_MASK = 1 << 1;
static const uint MATERIAL_FLAG_USE_NORMAL_TEXTURE = 1 << 2;
static const uint MATERIAL_FLAG_USE_METALLIC_ROUGHNESS_TEXTURE = 1 << 3;

struct MeshForwardVSOutput
{
    float4 position : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float4 color : COLOR0;
    float2 uv : TEXCOORD1;
};

struct MeshForwardPush
{
    float4 model_row0;
    float4 model_row1;
    float4 model_row2;
    float4 normal_row0;
    float4 normal_row1;
    float4 normal_row2;
    uint vertex_offset;
    uint index_offset;
    uint material_idx;
    uint _pad;
};

struct MeshViewParams
{
    float4x4 view_projection;
    float4x4 shadow_view_projection;
    float4 camera_position;
    float4 environment_radiance;
    float4 shadow_params;
    uint directional_light_count;
    uint point_light_count;
    uint shadow_directional_light_index;
    uint _pad0;
};

struct DirectionalLight
{
    float4 direction_intensity;
    float4 color;
};

struct PointLight
{
    float4 position_range;
    float4 color_intensity;
};

[[vk::push_constant]]
MeshForwardPush push;

[[vk::binding(0, 0)]]
StructuredBuffer<Mesh3DVertex> vertices;

[[vk::binding(1, 0)]]
StructuredBuffer<uint> indices;

[[vk::binding(2, 0)]]
StructuredBuffer<MaterialParams> materials;

[[vk::binding(3, 0)]]
StructuredBuffer<MeshViewParams> view_params;

[[vk::binding(4, 0)]]
StructuredBuffer<DirectionalLight> directional_lights;

[[vk::binding(5, 0)]]
StructuredBuffer<PointLight> point_lights;

[[vk::binding(6, 0)]]
Sampler2D shadow_map;

[[vk::binding(0, 1)]]
Sampler2D textures[];

float meshShadowFactor(MeshViewParams view, float3 world_pos, float3 normal, float3 light_dir)
{
    if (view.shadow_directional_light_index == 0xFFFFFFFF)
        return 1.0;

    float3 shadow_pos = world_pos + normal * view.shadow_params.y;
    float4 shadow_clip = mul(view.shadow_view_projection, float4(shadow_pos, 1.0));
    float shadow_w = shadow_clip.w;
    if (abs(shadow_w) < 1e-6)
        return 1.0;

    float3 shadow_ndc = shadow_clip.xyz / shadow_w;
    float2 shadow_uv = float2(
        shadow_ndc.x * 0.5 + 0.5,
        1.0 - (shadow_ndc.y * 0.5 + 0.5));
    if (shadow_uv.x <= 0.0 || shadow_uv.x >= 1.0 || shadow_uv.y <= 0.0 || shadow_uv.y >= 1.0)
        return 1.0;
    if (shadow_ndc.z <= 0.0 || shadow_ndc.z >= 1.0)
        return 1.0;

    float ndotl = max(dot(normal, light_dir), 0.0);
    float bias = view.shadow_params.x + (1.0 - ndotl) * view.shadow_params.y;
    float shadow_depth = shadow_map.Sample(shadow_uv).x;
    if (shadow_ndc.z - bias > shadow_depth)
        return 1.0 - saturate(view.shadow_params.z);
    return 1.0;
}

[shader("vertex")]
MeshForwardVSOutput vertexMain(uint vid : SV_VertexID)
{
    uint idx = indices[push.index_offset + vid];
    Mesh3DVertex v = vertices[push.vertex_offset + idx];
    MeshViewParams view = view_params[0];

    float4 local_pos = float4(v.position.xyz, 1.0);
    float3 world_pos = float3(
        dot(push.model_row0, local_pos),
        dot(push.model_row1, local_pos),
        dot(push.model_row2, local_pos));
    float3 model_x = push.model_row0.xyz;
    float3 model_y = push.model_row1.xyz;
    float3 model_z = push.model_row2.xyz;
    float3 normal_x = push.normal_row0.xyz;
    float3 normal_y = push.normal_row1.xyz;
    float3 normal_z = push.normal_row2.xyz;

    MeshForwardVSOutput o;
    o.position = mul(view.view_projection, float4(world_pos, 1.0));
    o.world_pos = world_pos;
    o.normal = normalize(normal_x * v.normal.x + normal_y * v.normal.y + normal_z * v.normal.z);
    float3 tangent = normalize(model_x * v.tangent.x + model_y * v.tangent.y + model_z * v.tangent.z);
    tangent = normalize(tangent - o.normal * dot(o.normal, tangent));
    o.tangent = float4(tangent, v.tangent.w);
    o.color = v.color;
    o.uv = v.uv;
    return o;
}

[shader("fragment")]
float4 fragmentMain(MeshForwardVSOutput input, bool is_front_face : SV_IsFrontFace) : SV_Target
{
    MaterialParams mat = materials[push.material_idx];
    MeshViewParams view = view_params[0];

    float4 surface = input.color * mat.base_color;
    if ((mat.flags & MATERIAL_FLAG_USE_BASE_COLOR_TEXTURE) != 0)
        surface *= textures[NonUniformResourceIndex(mat.base_color_texture_idx)].Sample(input.uv);

    if ((mat.flags & MATERIAL_FLAG_ALPHA_MASK) != 0 && surface.a < mat.alpha_cutoff)
        discard;

    float3 N = normalize(input.normal);
    float face_sign = is_front_face ? 1.0 : -1.0;
    N *= face_sign;
    if ((mat.flags & MATERIAL_FLAG_USE_NORMAL_TEXTURE) != 0)
    {
        float3 tangent_sample = textures[NonUniformResourceIndex(mat.normal_texture_idx)].Sample(input.uv).xyz * 2.0 - 1.0;
        tangent_sample.xy *= mat.normal_scale;

        float3 T = normalize(input.tangent.xyz) * face_sign;
        float3 B = normalize(cross(N, T) * input.tangent.w);
        N = normalize(T * tangent_sample.x + B * tangent_sample.y + N * tangent_sample.z);
    }

    float roughness = clamp(mat.roughness_factor, 0.04, 1.0);
    float metallic = clamp(mat.metallic_factor, 0.0, 1.0);
    if ((mat.flags & MATERIAL_FLAG_USE_METALLIC_ROUGHNESS_TEXTURE) != 0)
    {
        float4 mr = textures[NonUniformResourceIndex(mat.metallic_roughness_texture_idx)].Sample(input.uv);
        roughness = clamp(roughness * mr.g, 0.04, 1.0);
        metallic = clamp(metallic * mr.b, 0.0, 1.0);
    }

    float3 view_dir = normalize(view.camera_position.xyz - input.world_pos);
    float shininess = lerp(64.0, 4.0, roughness);
    float specular_strength = lerp(1.0, 0.25, roughness);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), surface.rgb, metallic);
    float3 environment_radiance = view.environment_radiance.rgb;
    float3 ambient = surface.rgb * environment_radiance * (1.0 - 0.35 * metallic);

    float3 lit = ambient;

    for (uint i = 0; i < view.directional_light_count; i++)
    {
        DirectionalLight light = directional_lights[i];
        float3 light_dir = light.direction_intensity.xyz;
        if (dot(light_dir, light_dir) > 0.000001)
            light_dir = normalize(light_dir);
        else
            light_dir = float3(0.0, 1.0, 0.0);

        float3 light_color = light.color.rgb * light.direction_intensity.w;
        if (i == view.shadow_directional_light_index)
            light_color *= meshShadowFactor(view, input.world_pos, N, light_dir);
        float3 half_dir = normalize(light_dir + view_dir);
        float ndotl = max(dot(N, light_dir), 0.0);
        float ndoth = max(dot(N, half_dir), 0.0);
        float3 diffuse = surface.rgb * ndotl * (1.0 - metallic) * light_color;
        float3 specular = f0 * pow(ndoth, shininess) * specular_strength * light_color;
        lit += diffuse + specular;
    }

    for (uint i = 0; i < view.point_light_count; i++)
    {
        PointLight light = point_lights[i];
        float3 to_light = light.position_range.xyz - input.world_pos;
        float dist_sq = dot(to_light, to_light);
        if (dist_sq <= 0.000001)
            continue;

        float range = max(light.position_range.w, 0.0001);
        float dist = sqrt(dist_sq);
        if (dist >= range)
            continue;

        float attenuation = saturate(1.0 - dist / range);
        attenuation *= attenuation;

        float3 light_dir = to_light / dist;
        float3 light_color = light.color_intensity.rgb * (light.color_intensity.w * attenuation);
        float3 half_dir = normalize(light_dir + view_dir);
        float ndotl = max(dot(N, light_dir), 0.0);
        float ndoth = max(dot(N, half_dir), 0.0);
        float3 diffuse = surface.rgb * ndotl * (1.0 - metallic) * light_color;
        float3 specular = f0 * pow(ndoth, shininess) * specular_strength * light_color;
        lit += diffuse + specular;
    }

    return float4(lit, surface.a);
}
