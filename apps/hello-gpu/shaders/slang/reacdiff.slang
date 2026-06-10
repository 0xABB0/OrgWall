#if defined(MEL_TARGET_METAL)

struct Root
{
    DescriptorHandle<Texture2D<float4> > tex;
    DescriptorHandle<SamplerState> smp;
    DescriptorHandle<RWTexture2D<float4> > img;
    uint w;
    uint h;
    float da;
    float db;
    float feed;
    float kill;
    float dt;
    float time;
};

[[vk::push_constant]]
Root root;

Texture2D<float4> mel_tex() { return root.tex; }
SamplerState mel_smp() { return root.smp; }
RWTexture2D<float4> mel_img() { return root.img; }

#else

struct Root
{
    uint tex;
    uint smp;
    uint img;
    uint w;
    uint h;
    float da;
    float db;
    float feed;
    float kill;
    float dt;
    float time;
};

[[vk::push_constant]]
Root root;

[[vk::binding(0, 0)]]
Texture2D<float4> u_textures[];

[[vk::binding(0, 1)]]
SamplerState u_samplers[];

[[vk::binding(0, 4)]]
[format("rgba8")]
RWTexture2D<float4> u_images[];

Texture2D<float4> mel_tex() { return u_textures[NonUniformResourceIndex(root.tex)]; }
SamplerState mel_smp() { return u_samplers[NonUniformResourceIndex(root.smp)]; }
RWTexture2D<float4> mel_img() { return u_images[root.img]; }

#endif

float rd_hash(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_init(uint3 tid : SV_DispatchThreadID)
{
    uint2 px = tid.xy;
    if (px.x >= root.w || px.y >= root.h)
        return;

    float2 uv = float2(px) / float2(float(root.w), float(root.h));
    float seed = rd_hash(uv);
    float cx = (uv.x - 0.5) * float(root.w) / 64.0;
    float cy = (uv.y - 0.5) * float(root.h) / 64.0;
    float inside = float(cx * cx + cy * cy < 1.5 || seed > 0.93);
    float b = inside;
    float a = 1.0 - inside;
    mel_img()[int2(px)] = float4(a, b, 0.0, 1.0);
}

float2 rd_sample_ab(float2 uv)
{
    return mel_tex().SampleLevel(mel_smp(), uv, 0.0).xy;
}

float2 rd_laplacian(float2 uv, float px, float py)
{
    float2 c = rd_sample_ab(uv);
    float2 l = rd_sample_ab(uv + float2(-px, 0.0));
    float2 r = rd_sample_ab(uv + float2(px, 0.0));
    float2 u = rd_sample_ab(uv + float2(0.0, -py));
    float2 d = rd_sample_ab(uv + float2(0.0, py));
    float2 ul = rd_sample_ab(uv + float2(-px, -py));
    float2 ur = rd_sample_ab(uv + float2(px, -py));
    float2 dl = rd_sample_ab(uv + float2(-px, py));
    float2 dr = rd_sample_ab(uv + float2(px, py));
    return -c + 0.2 * (l + r + u + d) + 0.05 * (ul + ur + dl + dr);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_step(uint3 tid : SV_DispatchThreadID)
{
    uint2 gid = tid.xy;
    if (gid.x >= root.w || gid.y >= root.h)
        return;

    float pw = 1.0 / float(root.w);
    float ph = 1.0 / float(root.h);
    float2 uv = (float2(gid) + 0.5) * float2(pw, ph);

    float2 c = rd_sample_ab(uv);
    float a = c.x;
    float b = c.y;

    float2 lap = rd_laplacian(uv, pw, ph);
    float reaction = a * b * b;
    float na = clamp(a + (root.da * lap.x - reaction + root.feed * (1.0 - a)) * root.dt, 0.0, 1.0);
    float nb = clamp(b + (root.db * lap.y + reaction - (root.kill + root.feed) * b) * root.dt, 0.0, 1.0);
    mel_img()[int2(gid)] = float4(na, nb, 0.0, 1.0);
}

float3 rd_palette(float t)
{
    float3 a = float3(0.5, 0.5, 0.5);
    float3 b = float3(0.5, 0.5, 0.5);
    float3 c = float3(1.0, 1.0, 0.9);
    float3 d = float3(0.0, 0.15, 0.40);
    return a + b * cos(6.28318530 * (c * t + d));
}

struct VOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VOut vs_draw(uint vid : SV_VertexID)
{
    VOut o;
    float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    o.uv = p;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fs_draw(VOut input) : SV_Target
{
    float2 ab = mel_tex().Sample(mel_smp(), input.uv).xy;
    float t = ab.x - ab.y;
    t = clamp(t * 0.5 + 0.5, 0.0, 1.0);
    return float4(rd_palette(t), 1.0);
}
