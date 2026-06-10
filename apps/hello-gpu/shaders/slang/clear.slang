#if defined(MEL_TARGET_METAL)

struct Root
{
    DescriptorHandle<RWTexture2D<float4> > image;
    uint w;
    uint h;
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
    float time;
};

[[vk::push_constant]]
Root root;

[[vk::binding(0, 4)]]
[format("rgba8")]
RWTexture2D<float4> u_images[];

RWTexture2D<float4> mel_storage_image() { return u_images[NonUniformResourceIndex(root.image)]; }

#endif

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint2 px = tid.xy;
    if (px.x >= root.w || px.y >= root.h)
        return;

    float2 uv = (float2(px) + 0.5) / float2(root.w, root.h);
    float r = distance(uv, float2(0.5, 0.5));
    float3 base = lerp(float3(0.05, 0.06, 0.10), float3(0.01, 0.02, 0.04), r * 1.4);
    base += 0.015 * sin(uv.x * 40.0 + root.time) * sin(uv.y * 40.0 - root.time);
    mel_storage_image()[int2(px)] = float4(base, 1.0);
}
