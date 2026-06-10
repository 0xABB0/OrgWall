#if defined(MEL_TARGET_METAL)

struct Root
{
    DescriptorHandle<Texture2D<float4> > tex0;
    DescriptorHandle<Texture2D<float4> > tex1;
    DescriptorHandle<SamplerState> smp;
    DescriptorHandle<RWTexture2D<float4> > img;
    uint w;
    uint h;
    float param0;
    float param1;
};

[[vk::push_constant]]
Root root;

Texture2D<float4> mel_tex0() { return root.tex0; }
Texture2D<float4> mel_tex1() { return root.tex1; }
SamplerState mel_smp() { return root.smp; }
RWTexture2D<float4> mel_img() { return root.img; }

#else

struct Root
{
    uint tex0;
    uint tex1;
    uint smp;
    uint img;
    uint w;
    uint h;
    float param0;
    float param1;
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

Texture2D<float4> mel_tex0() { return u_textures[NonUniformResourceIndex(root.tex0)]; }
Texture2D<float4> mel_tex1() { return u_textures[NonUniformResourceIndex(root.tex1)]; }
SamplerState mel_smp() { return u_samplers[NonUniformResourceIndex(root.smp)]; }
RWTexture2D<float4> mel_img() { return u_images[root.img]; }

#endif

float3 sphere_hit(float3 ro, float3 rd, float3 center, float radius, float3 col)
{
    float3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float h = b * b - c;
    if (h < 0.0)
        return float3(0.0, 0.0, 0.0);
    float t = -b - sqrt(h);
    if (t < 0.0)
        return float3(0.0, 0.0, 0.0);
    float3 p = ro + t * rd;
    float3 n = normalize(p - center);
    float3 ld = normalize(float3(2.0, 3.0, 1.0));
    float diff = max(dot(n, ld), 0.0);
    float spec = pow(max(dot(reflect(-ld, n), -rd), 0.0), 32.0);
    return col * (0.1 + 0.9 * diff) + float3(spec * 0.6, spec * 0.6, spec * 0.6);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_scene(uint3 tid : SV_DispatchThreadID)
{
    uint2 px = tid.xy;
    if (px.x >= root.w || px.y >= root.h)
        return;

    float aspect = float(root.w) / float(root.h);
    float2 uv = (float2(px) + 0.5) / float2(float(root.w), float(root.h)) * 2.0 - 1.0;
    uv.x *= aspect;

    float3 ro = float3(0.0, 0.0, -4.0);
    float3 rd = normalize(float3(uv, 1.8));
    float t = root.param0;

    float3 col = float3(0.0, 0.0, 0.0);

    col += sphere_hit(ro, rd, float3(1.6 * cos(t * 0.7), 0.9 * sin(t * 0.5), 0.0), 0.55, float3(2.8, 0.4, 0.2));
    col += sphere_hit(ro, rd, float3(1.6 * cos(t * 0.7 + 2.094), 0.9 * sin(t * 0.5 + 2.094), 0.5 * sin(t * 0.3)), 0.55, float3(0.3, 2.4, 0.4));
    col += sphere_hit(ro, rd, float3(1.6 * cos(t * 0.7 + 4.189), 0.9 * sin(t * 0.5 + 4.189), 0.5 * sin(t * 0.3 + 1.0)), 0.55, float3(0.3, 0.4, 3.0));
    col += sphere_hit(ro, rd, float3(0.0, 0.0, 1.2 * sin(t * 0.4)), 0.55, float3(2.4, 1.8, 0.2));

    mel_img()[int2(px)] = float4(col, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_bright(uint3 tid : SV_DispatchThreadID)
{
    uint2 px = tid.xy;
    if (px.x >= root.w || px.y >= root.h)
        return;

    float2 uv = (float2(px) + 0.5) / float2(float(root.w), float(root.h));
    float3 col = mel_tex0().SampleLevel(mel_smp(), uv, 0.0).rgb;
    float lum = dot(col, float3(0.2126, 0.7152, 0.0722));
    float3 bright = (lum > root.param0) ? col * (lum - root.param0) / max(lum, 0.001) : float3(0.0, 0.0, 0.0);
    mel_img()[int2(px)] = float4(bright, 1.0);
}

static const float BLUR_WEIGHTS[7] = { 0.0625, 0.125, 0.1875, 0.25, 0.1875, 0.125, 0.0625 };

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_blurx(uint3 tid : SV_DispatchThreadID)
{
    uint2 px = tid.xy;
    if (px.x >= root.w || px.y >= root.h)
        return;

    float w = float(root.w);
    float h = float(root.h);
    float cy = (float(px.y) + 0.5) / h;

    float3 acc = float3(0.0, 0.0, 0.0);
    for (int i = -3; i <= 3; ++i)
    {
        float cx = (float(px.x) + 0.5 + float(i)) / w;
        acc += mel_tex0().SampleLevel(mel_smp(), float2(cx, cy), 0.0).rgb * BLUR_WEIGHTS[i + 3];
    }
    mel_img()[int2(px)] = float4(acc, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_blury(uint3 tid : SV_DispatchThreadID)
{
    uint2 px = tid.xy;
    if (px.x >= root.w || px.y >= root.h)
        return;

    float w = float(root.w);
    float h = float(root.h);
    float cx = (float(px.x) + 0.5) / w;

    float3 acc = float3(0.0, 0.0, 0.0);
    for (int i = -3; i <= 3; ++i)
    {
        float cy = (float(px.y) + 0.5 + float(i)) / h;
        acc += mel_tex0().SampleLevel(mel_smp(), float2(cx, cy), 0.0).rgb * BLUR_WEIGHTS[i + 3];
    }
    mel_img()[int2(px)] = float4(acc, 1.0);
}

struct VOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VOut vs_composite(uint vid : SV_VertexID)
{
    VOut o;
    float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    o.uv = p;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fs_composite(VOut input) : SV_Target
{
    float3 scene = mel_tex0().Sample(mel_smp(), input.uv).rgb;
    float3 bloom = mel_tex1().Sample(mel_smp(), input.uv).rgb;
    float3 hdr = scene + bloom * root.param0;
    float3 tone = hdr / (hdr + float3(1.0, 1.0, 1.0));
    return float4(pow(tone, float3(0.4545454545, 0.4545454545, 0.4545454545)), 1.0);
}
