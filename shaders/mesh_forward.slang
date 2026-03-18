struct Mesh3DVertex
{
    float3 position;
    float3 normal;
    float4 color;
};

struct MaterialParams
{
    float4 base_color;
};

struct MeshForwardVSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float4 color : COLOR0;
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

[shader("vertex")]
MeshForwardVSOutput vertexMain(uint vid : SV_VertexID)
{
    uint idx = indices[push.index_offset + vid];
    Mesh3DVertex v = vertices[push.vertex_offset + idx];
    MaterialParams mat = materials[push.material_idx];

    MeshForwardVSOutput o;
    o.position = mul(push.mvp, float4(v.position, 1.0));
    o.normal = v.normal;
    o.color = v.color * mat.base_color;
    return o;
}

[shader("fragment")]
float4 fragmentMain(MeshForwardVSOutput input) : SV_Target
{
    float3 light_dir = normalize(float3(0.3, 1.0, 0.5));
    float ndotl = max(dot(normalize(input.normal), light_dir), 0.0);
    float3 lit = input.color.rgb * (0.15 + 0.85 * ndotl);
    return float4(lit, input.color.a);
}
