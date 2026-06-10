struct Particle
{
    float4 pos_life;
    float4 vel;
};

#if defined(MEL_TARGET_METAL)

struct Root
{
    DescriptorHandle<RWStructuredBuffer<Particle> > particles_rw;
    DescriptorHandle<StructuredBuffer<Particle> > particles_ro;
    uint total;
    float dt;
    float time;
    float attract_x;
    float attract_y;
    float aspect;
};

[[vk::push_constant]]
Root root;

RWStructuredBuffer<Particle> mel_particles_rw() { return root.particles_rw; }
StructuredBuffer<Particle> mel_particles_ro() { return root.particles_ro; }

#else

struct Root
{
    uint particles_rw;
    uint particles_ro;
    uint total;
    float dt;
    float time;
    float attract_x;
    float attract_y;
    float aspect;
};

[[vk::push_constant]]
Root root;

[[vk::binding(0, 2)]]
RWStructuredBuffer<Particle> u_rw_buffers[];

[[vk::binding(0, 2)]]
StructuredBuffer<Particle> u_ro_buffers[];

RWStructuredBuffer<Particle> mel_particles_rw() { return u_rw_buffers[root.particles_rw]; }
StructuredBuffer<Particle> mel_particles_ro() { return u_ro_buffers[NonUniformResourceIndex(root.particles_ro)]; }

#endif

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_sim(uint3 tid : SV_DispatchThreadID)
{
    uint i = tid.x;
    if (i >= root.total)
        return;

    Particle pt = mel_particles_rw()[i];
    float2 attract = float2(root.attract_x, root.attract_y);

    float2 to = attract - pt.pos_life.xy;
    float d2 = max(dot(to, to), 0.01);
    float2 accel = to / d2 * 0.10;

    pt.vel.xy = pt.vel.xy * 0.992 + accel * root.dt;
    pt.pos_life.xy += pt.vel.xy * root.dt;
    pt.pos_life.z -= root.dt * 0.25;

    if (pt.pos_life.z <= 0.0 || abs(pt.pos_life.x) > 1.3 || abs(pt.pos_life.y) > 1.3)
    {
        float a = pt.pos_life.w + root.time * 1.7 + float(i) * 0.013;
        float r = 0.9 + 0.1 * frac(sin(float(i) * 12.9898) * 43758.5453);
        pt.pos_life.xy = r * float2(cos(a), sin(a));
        pt.vel.xy = 0.15 * float2(-sin(a), cos(a));
        pt.pos_life.z = 1.0;
    }

    mel_particles_rw()[i] = pt;
}

struct VOut
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
VOut vs_draw(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    const float2 corners[6] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
        float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0),
    };

    Particle pt = mel_particles_ro()[iid];
    float life = clamp(pt.pos_life.z, 0.0, 1.0);
    float size = 0.004 + 0.012 * life;

    float2 corner = corners[vid];
    float2 offset = corner * size;
    if (root.aspect >= 1.0)
        offset.x /= root.aspect;
    else
        offset.y *= root.aspect;
    float2 ndc = pt.pos_life.xy + offset;

    float speed = length(pt.vel.xy);
    float3 cool = float3(0.15, 0.45, 0.95);
    float3 hot = float3(1.0, 0.65, 0.15);
    float3 col = lerp(cool, hot, clamp(speed * 3.0, 0.0, 1.0));

    VOut o;
    o.color = float4(col * (0.35 + 0.65 * life), life);
    o.pos = float4(ndc, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fs_draw(VOut input) : SV_Target
{
    return input.color;
}
