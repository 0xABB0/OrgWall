struct Agent
{
    float4 pos_phase;
};

#if defined(MEL_TARGET_METAL)

struct Root
{
    DescriptorHandle<StructuredBuffer<Agent> > agents;
    DescriptorHandle<RWStructuredBuffer<uint> > survivors;
    DescriptorHandle<RWStructuredBuffer<uint> > args;
    DescriptorHandle<RWTexture2D<float4> > image;
    uint total;
    uint w;
    uint h;
    uint local;
    float time;
    float cull_r;
    float cull_x;
    float cull_y;
};

[[vk::push_constant]]
Root root;

StructuredBuffer<Agent> mel_agents() { return root.agents; }
RWStructuredBuffer<uint> mel_survivors() { return root.survivors; }
RWStructuredBuffer<uint> mel_args() { return root.args; }
RWTexture2D<float4> mel_image() { return root.image; }

#else

struct Root
{
    uint agents;
    uint survivors;
    uint args;
    uint image;
    uint total;
    uint w;
    uint h;
    uint local;
    float time;
    float cull_r;
    float cull_x;
    float cull_y;
};

[[vk::push_constant]]
Root root;

[[vk::binding(0, 2)]]
StructuredBuffer<Agent> u_agent_buffers[];

[[vk::binding(0, 2)]]
RWStructuredBuffer<uint> u_uint_buffers[];

[[vk::binding(0, 4)]]
[format("rgba8")]
RWTexture2D<float4> u_images[];

StructuredBuffer<Agent> mel_agents() { return u_agent_buffers[NonUniformResourceIndex(root.agents)]; }
RWStructuredBuffer<uint> mel_survivors() { return u_uint_buffers[root.survivors]; }
RWStructuredBuffer<uint> mel_args() { return u_uint_buffers[root.args]; }
RWTexture2D<float4> mel_image() { return u_images[root.image]; }

#endif

static const uint SURV_IDX_BASE = 4;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_cull(uint3 tid : SV_DispatchThreadID)
{
    uint i = tid.x;
    if (i >= root.total)
        return;

    Agent ag = mel_agents()[i];
    float ang = ag.pos_phase.w + root.time * 0.6;
    float2 p = ag.pos_phase.xy + ag.pos_phase.z * float2(cos(ang), sin(ang));

    if (distance(p, float2(root.cull_x, root.cull_y)) <= root.cull_r)
    {
        uint slot;
        InterlockedAdd(mel_survivors()[0], 1u, slot);
        mel_survivors()[SURV_IDX_BASE + slot] = i;
    }
}

[shader("compute")]
[numthreads(1, 1, 1)]
void cs_args(uint3 tid : SV_DispatchThreadID)
{
    uint n = mel_survivors()[0];
    uint gx = (n + root.local - 1u) / root.local;
    mel_args()[0] = gx;
    mel_args()[1] = 1u;
    mel_args()[2] = 1u;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_clear(uint3 tid : SV_DispatchThreadID)
{
    uint2 px = tid.xy;
    if (px.x >= root.w || px.y >= root.h)
        return;
    float2 uv = (float2(px) + 0.5) / float2(float(root.w), float(root.h));
    float r = distance(uv, float2(0.5, 0.5));
    float3 base = lerp(float3(0.05, 0.06, 0.10), float3(0.01, 0.02, 0.04), r * 1.4);
    base += 0.015 * sin(uv.x * 40.0 + root.time) * sin(uv.y * 40.0 - root.time);
    mel_image()[int2(px)] = float4(base, 1.0);
}

void shade_splat(uint i)
{
    Agent ag = mel_agents()[i];
    float ang = ag.pos_phase.w + root.time * 0.6;
    float2 p = ag.pos_phase.xy + ag.pos_phase.z * float2(cos(ang), sin(ang));

    float2 centre = (p * 0.5 + 0.5) * float2(float(root.w), float(root.h));
    float hue = float(i) / float(root.total);
    float3 col = 0.5 + 0.5 * cos(6.2831 * (hue + float3(0.0, 0.33, 0.67)));

    int rad = 7;
    int2 c = int2(centre);
    for (int dy = -rad; dy <= rad; ++dy)
        for (int dx = -rad; dx <= rad; ++dx)
        {
            int2 px = c + int2(dx, dy);
            if (px.x < 0 || px.y < 0 || px.x >= int(root.w) || px.y >= int(root.h))
                continue;
            float d = length(float2(float(dx), float(dy))) / float(rad);
            if (d > 1.0)
                continue;
            float fall = 1.0 - d;
            float4 prev = mel_image()[px];
            mel_image()[px] = float4(prev.rgb + col * fall * fall * 0.9, 1.0);
        }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_shade(uint3 gid : SV_GroupID, uint3 ltid : SV_GroupThreadID)
{
    if (gid.x != 0u || ltid.x != 0u)
        return;

    uint count = mel_survivors()[0];
    uint emitted = 0u;
    for (uint i = 0u; i < root.total && emitted < count; ++i)
    {
        float ang = mel_agents()[i].pos_phase.w + root.time * 0.6;
        float2 p = mel_agents()[i].pos_phase.xy + mel_agents()[i].pos_phase.z * float2(cos(ang), sin(ang));
        if (distance(p, float2(root.cull_x, root.cull_y)) <= root.cull_r)
        {
            shade_splat(i);
            ++emitted;
        }
    }
}
