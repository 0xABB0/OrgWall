#if defined(MEL_TARGET_METAL)

struct Root
{
    DescriptorHandle<RWTexture2D<float4> > image;
    uint w;
    uint h;
    uint max_iter;
    float center_x;
    float center_y;
    float scale;
    float time;
};

[[vk::push_constant]]
Root root;

RWTexture2D<float4> mel_storage_image() { return root.image; }

#else

struct Root
{
    uint image;
    uint w;
    uint h;
    uint max_iter;
    float center_x;
    float center_y;
    float scale;
    float time;
};

[[vk::push_constant]]
Root root;

[[vk::binding(0, 4)]]
[format("rgba8")]
RWTexture2D<float4> u_images[];

RWTexture2D<float4> mel_storage_image() { return u_images[root.image]; }

#endif

float3 palette(float t)
{
    float3 a = float3(0.5, 0.5, 0.5);
    float3 b = float3(0.5, 0.5, 0.5);
    float3 c = float3(1.0, 1.0, 1.0);
    float3 d = float3(0.0, 0.10, 0.20);
    return a + b * cos(6.2831853 * (c * t + d));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint2 px = tid.xy;
    if (px.x >= root.w || px.y >= root.h)
        return;

    float aspect = float(root.w) / float(root.h);
    float cx = (float(px.x) + 0.5) / float(root.w) - 0.5;
    float cy = (float(px.y) + 0.5) / float(root.h) - 0.5;
    cx *= aspect;

    float re = root.center_x + cx * root.scale;
    float im = root.center_y + cy * root.scale;

    float zr = 0.0;
    float zi = 0.0;
    uint i = 0;
    float mag2 = 0.0;
    for (; i < root.max_iter; ++i)
    {
        float zr2 = zr * zr;
        float zi2 = zi * zi;
        mag2 = zr2 + zi2;
        if (mag2 > 65536.0)
            break;
        zi = 2.0 * zr * zi + im;
        zr = zr2 - zi2 + re;
    }

    float3 col;
    if (i >= root.max_iter)
    {
        col = float3(0.0, 0.0, 0.0);
    }
    else
    {
        float fi = float(i);
        float smooth_i = fi + 1.0 - log2(log(mag2) * 0.5);
        float t = smooth_i / float(root.max_iter);
        t = pow(clamp(t, 0.0, 1.0), 0.5);
        col = palette(t * 2.5 + root.time * 0.04);
        col *= 0.35 + 0.65 * clamp(smooth_i * 0.04, 0.0, 1.0);
    }

    mel_storage_image()[int2(px)] = float4(col, 1.0);
}
