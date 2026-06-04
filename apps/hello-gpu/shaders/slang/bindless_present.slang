#if defined(MEL_TARGET_METAL)

struct Root
{
    DescriptorHandle<Texture2D<float4> > tex;
    DescriptorHandle<SamplerState> smp;
};

[[vk::push_constant]]
Root root;

Texture2D<float4> mel_bindless_texture() { return root.tex; }
SamplerState mel_bindless_sampler() { return root.smp; }

#else

struct Root
{
    uint tex;
    uint smp;
};

[[vk::push_constant]]
Root root;

[[vk::binding(0, 0)]]
Texture2D u_textures[];

[[vk::binding(1, 0)]]
SamplerState u_samplers[];

Texture2D<float4> mel_bindless_texture() { return u_textures[NonUniformResourceIndex(root.tex)]; }
SamplerState mel_bindless_sampler() { return u_samplers[NonUniformResourceIndex(root.smp)]; }

#endif

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
    Texture2D<float4> t = mel_bindless_texture();
    SamplerState s = mel_bindless_sampler();
    return t.Sample(s, input.uv);
}
