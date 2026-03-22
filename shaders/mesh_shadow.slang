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

struct MeshShadowPush
{
    float4 model_row0;
    float4 model_row1;
    float4 model_row2;
    uint vertex_offset;
    uint index_offset;
    uint material_idx;
    uint _pad;
};

struct ShadowViewParams
{
    float4x4 light_view_projection;
};

struct MeshShadowVSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[[vk::push_constant]]
MeshShadowPush push;

[[vk::binding(0, 0)]]
StructuredBuffer<Mesh3DVertex> vertices;

[[vk::binding(1, 0)]]
StructuredBuffer<uint> indices;

[[vk::binding(2, 0)]]
StructuredBuffer<MaterialParams> materials;

[[vk::binding(3, 0)]]
StructuredBuffer<ShadowViewParams> shadow_view_params;

[[vk::binding(0, 1)]]
Sampler2D textures[];

[shader("vertex")]
MeshShadowVSOutput vertexMain(uint vid : SV_VertexID)
{
    uint idx = indices[push.index_offset + vid];
    Mesh3DVertex v = vertices[push.vertex_offset + idx];
    ShadowViewParams shadow_view = shadow_view_params[0];

    float4 local_pos = float4(v.position.xyz, 1.0);
    float3 world_pos = float3(
        dot(push.model_row0, local_pos),
        dot(push.model_row1, local_pos),
        dot(push.model_row2, local_pos));

    MeshShadowVSOutput o;
    o.position = mul(shadow_view.light_view_projection, float4(world_pos, 1.0));
    o.uv = v.uv;
    return o;
}

[shader("fragment")]
void fragmentMain(MeshShadowVSOutput input)
{
    MaterialParams mat = materials[push.material_idx];
    if ((mat.flags & MATERIAL_FLAG_ALPHA_MASK) == 0)
        return;

    float alpha = mat.base_color.a;
    if ((mat.flags & MATERIAL_FLAG_USE_BASE_COLOR_TEXTURE) != 0)
        alpha *= textures[NonUniformResourceIndex(mat.base_color_texture_idx)].Sample(input.uv).a;

    if (alpha < mat.alpha_cutoff)
        discard;
}
