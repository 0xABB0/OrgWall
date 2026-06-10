struct Root
{
    uint tex;
    uint smp;
};

[[vk::push_constant]]
Root root;

[[vk::binding(0, 0)]]
Texture2D u_textures[];

[[vk::binding(0, 1)]]
SamplerState u_samplers[];

struct VOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VOut vs_main(uint vid : SV_VertexID)
{
    VOut o;
    float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    o.uv = p;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fs_main(VOut input) : SV_Target
{
    Texture2D t = u_textures[NonUniformResourceIndex(root.tex)];
    SamplerState s = u_samplers[NonUniformResourceIndex(root.smp)];
    return t.Sample(s, input.uv);
}
