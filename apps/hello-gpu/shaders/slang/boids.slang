struct Boid
{
    float4 pos_vel;
};

#if defined(MEL_TARGET_METAL)

struct Root
{
    DescriptorHandle<StructuredBuffer<Boid> > src;
    DescriptorHandle<RWStructuredBuffer<Boid> > dst;
    uint total;
    uint pad;
    float dt;
    float time;
    float goal_x;
    float goal_y;
    float aspect;
    float pad1;
};

[[vk::push_constant]]
Root root;

StructuredBuffer<Boid> mel_src() { return root.src; }
RWStructuredBuffer<Boid> mel_dst() { return root.dst; }

#else

struct Root
{
    uint src;
    uint dst;
    uint total;
    uint pad;
    float dt;
    float time;
    float goal_x;
    float goal_y;
    float aspect;
    float pad1;
};

[[vk::push_constant]]
Root root;

[[vk::binding(0, 2)]]
StructuredBuffer<Boid> u_src_buffers[];

[[vk::binding(0, 2)]]
RWStructuredBuffer<Boid> u_dst_buffers[];

StructuredBuffer<Boid> mel_src() { return u_src_buffers[NonUniformResourceIndex(root.src)]; }
RWStructuredBuffer<Boid> mel_dst() { return u_dst_buffers[root.dst]; }

#endif

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_sim(uint3 tid : SV_DispatchThreadID)
{
    uint gid = tid.x;
    if (gid >= root.total)
        return;

    Boid me = mel_src()[gid];
    float2 pos = me.pos_vel.xy;
    float2 vel = me.pos_vel.zw;

    float2 sep = float2(0.0, 0.0);
    float2 ali = float2(0.0, 0.0);
    float2 coh = float2(0.0, 0.0);
    float ali_n = 0.0;
    float coh_n = 0.0;

    const float r_sep = 0.045;
    const float r_view = 0.13;

    for (uint i = 0; i < root.total; ++i)
    {
        if (i == gid)
            continue;
        Boid ob = mel_src()[i];
        float2 d = ob.pos_vel.xy - pos;
        float dist2 = dot(d, d);
        if (dist2 > r_view * r_view)
            continue;
        float dist = sqrt(max(dist2, 1e-6));
        if (dist < r_sep)
            sep -= d / dist * (r_sep - dist);
        ali += ob.pos_vel.zw;
        ali_n += 1.0;
        coh += ob.pos_vel.xy;
        coh_n += 1.0;
    }

    float2 acc = float2(0.0, 0.0);
    acc += sep * 2.6;
    if (ali_n > 0.0)
        acc += (ali / ali_n - vel) * 1.1;
    if (coh_n > 0.0)
        acc += (coh / coh_n - pos) * 0.9;

    float2 goal = float2(root.goal_x, root.goal_y);
    acc += (goal - pos) * 0.35;

    vel += acc * root.dt;

    float sp = length(vel);
    const float max_sp = 0.55;
    const float min_sp = 0.18;
    if (sp > max_sp)
        vel = vel / sp * max_sp;
    else if (sp < min_sp && sp > 1e-4)
        vel = vel / sp * min_sp;

    pos += vel * root.dt;

    if (pos.x < -1.0) { pos.x = -1.0; vel.x = abs(vel.x); }
    if (pos.x > 1.0) { pos.x = 1.0; vel.x = -abs(vel.x); }
    if (pos.y < -1.0) { pos.y = -1.0; vel.y = abs(vel.y); }
    if (pos.y > 1.0) { pos.y = 1.0; vel.y = -abs(vel.y); }

    Boid result;
    result.pos_vel = float4(pos, vel);
    mel_dst()[gid] = result;
}

struct VOut
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
VOut vs_draw(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    const float2 shape[3] = {
        float2(0.018, 0.0),
        float2(-0.012, 0.008),
        float2(-0.012, -0.008),
    };

    Boid me = mel_src()[iid];
    float2 pos = me.pos_vel.xy;
    float2 vel = me.pos_vel.zw;

    float sp = length(vel);
    float2 dir = sp > 1e-4 ? vel / sp : float2(1.0, 0.0);
    float2 perp = float2(-dir.y, dir.x);

    float2 local = shape[vid];
    float2 world = pos + dir * local.x + perp * local.y;

    if (root.aspect >= 1.0)
        world.x /= root.aspect;
    else
        world.y *= root.aspect;

    float hue = atan2(dir.y, dir.x);
    float3 col = 0.55 + 0.45 * cos(float3(0.0, 2.094, 4.188) + hue + root.time * 0.1);
    col = lerp(float3(0.4, 0.5, 0.7), col, clamp(sp * 2.0, 0.3, 1.0));

    VOut o;
    o.color = float4(col, 1.0);
    o.pos = float4(world, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fs_draw(VOut input) : SV_Target
{
    return input.color;
}
