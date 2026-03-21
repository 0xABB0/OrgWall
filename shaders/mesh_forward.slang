struct Mesh3DVertex
{
    float4 position;
    float4 normal;
    float4 color;
    float2 uv;
    float2 _pad;
};

struct MaterialParams
{
    float4 base_color;
    uint base_color_texture_idx;
    uint flags;
    float alpha_cutoff;
    float _pad;
};

static const uint MATERIAL_FLAG_USE_BASE_COLOR_TEXTURE = 1 << 0;
static const uint MATERIAL_FLAG_ALPHA_MASK = 1 << 1;

struct MeshForwardVSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    nointerpolation uint texture_idx : TEXCOORD1;
    nointerpolation uint flags : TEXCOORD2;
    nointerpolation float alpha_cutoff : TEXCOORD3;
};

struct MeshForwardPush
{
    float4x4 mvp;
    uint vertex_offset;
    uint index_offset;
    uint material_idx;
    uint _pad;
};

[[vk::push_constant]]
MeshForwardPush push;

[[vk::binding(0, 0)]]
StructuredBuffer<Mesh3DVertex> vertices;

[[vk::binding(1, 0)]]
StructuredBuffer<uint> indices;

[[vk::binding(2, 0)]]
StructuredBuffer<MaterialParams> materials;

[[vk::binding(0, 1)]]
Sampler2D textures[];

[shader("vertex")]
MeshForwardVSOutput vertexMain(uint vid : SV_VertexID)
{
    uint idx = indices[push.index_offset + vid];
    Mesh3DVertex v = vertices[push.vertex_offset + idx];
    MaterialParams mat = materials[push.material_idx];

    MeshForwardVSOutput o;
    o.position = mul(push.mvp, float4(v.position.xyz, 1.0));
    o.normal = v.normal.xyz;
    o.color = v.color * mat.base_color;
    o.uv = v.uv;
    o.texture_idx = mat.base_color_texture_idx;
    o.flags = mat.flags;
    o.alpha_cutoff = mat.alpha_cutoff;
    return o;
}

[shader("fragment")]
float4 fragmentMain(MeshForwardVSOutput input) : SV_Target
{
    float4 surface = input.color;
    if ((input.flags & MATERIAL_FLAG_USE_BASE_COLOR_TEXTURE) != 0)
        surface *= textures[NonUniformResourceIndex(input.texture_idx)].Sample(input.uv);

    if ((input.flags & MATERIAL_FLAG_ALPHA_MASK) != 0 && surface.a < input.alpha_cutoff)
        discard;

    float3 light_dir = normalize(float3(0.3, 1.0, 0.5));
    float ndotl = max(dot(normalize(input.normal), light_dir), 0.0);
    float3 lit = surface.rgb * (0.15 + 0.85 * ndotl);
    return float4(lit, surface.a);
}
