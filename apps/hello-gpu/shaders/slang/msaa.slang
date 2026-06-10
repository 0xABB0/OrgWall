#if defined(MEL_TARGET_METAL)

struct Root
{
    DescriptorHandle<Texture2D<float4> > resolved;
    DescriptorHandle<Texture2D<float4> > reference;
    DescriptorHandle<SamplerState> smp;
    float angle;
    float aspect;
    float pad0;
    float pad1;
};

[[vk::push_constant]]
Root root;

Texture2D<float4> mel_resolved() { return root.resolved; }
Texture2D<float4> mel_reference() { return root.reference; }
SamplerState mel_smp() { return root.smp; }

#else

struct Root
{
    uint resolved;
    uint reference;
    uint smp;
    float angle;
    float aspect;
    float pad0;
    float pad1;
};

[[vk::push_constant]]
Root root;

[[vk::binding(0, 0)]]
Texture2D<float4> u_textures[];

[[vk::binding(0, 1)]]
SamplerState u_samplers[];

Texture2D<float4> mel_resolved() { return u_textures[NonUniformResourceIndex(root.resolved)]; }
Texture2D<float4> mel_reference() { return u_textures[NonUniformResourceIndex(root.reference)]; }
SamplerState mel_smp() { return u_samplers[NonUniformResourceIndex(root.smp)]; }

#endif

struct VIn
{
    float2 pos : POSITION;
    float4 color : COLOR0;
};

struct VOut
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
VOut vs_star(VIn input)
{
    float c = cos(root.angle);
    float s = sin(root.angle);
    float2 r = float2(input.pos.x * c - input.pos.y * s, input.pos.x * s + input.pos.y * c);
    if (root.aspect >= 1.0)
        r.x /= root.aspect;
    else
        r.y *= root.aspect;

    VOut o;
    o.color = input.color;
    o.pos = float4(r, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fs_star(VOut input) : SV_Target
{
    return input.color;
}

struct CVOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
CVOut vs_compose(uint vid : SV_VertexID)
{
    CVOut o;
    float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    o.uv = p;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fs_compose(CVOut input) : SV_Target
{
    bool left = input.uv.x < 0.5;
    float2 uv = float2(left ? input.uv.x * 2.0 : (input.uv.x - 0.5) * 2.0, input.uv.y);

    SamplerState s = mel_smp();
    float4 c = left ? mel_resolved().Sample(s, uv) : mel_reference().Sample(s, uv);

    float seam = smoothstep(0.0015, 0.0, abs(input.uv.x - 0.5));
    return lerp(c, float4(0.0, 0.0, 0.0, 1.0), seam);
}
