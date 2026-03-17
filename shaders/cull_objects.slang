struct AABB
{
    float3 center;
    float _pad0;
    float3 extents;
    float _pad1;
};

struct Frustum
{
    float4 planes[6];
};

struct CullParams
{
    uint object_count;
};

[[vk::push_constant]]
CullParams params;

[[vk::binding(0, 0)]]
StructuredBuffer<AABB> bounds;

[[vk::binding(1, 0)]]
StructuredBuffer<Frustum> frustum_buf;

[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> visibility;

bool aabb_vs_plane(AABB aabb, float4 plane)
{
    float3 normal = plane.xyz;
    float d = plane.w;
    float r = dot(abs(normal), aabb.extents);
    float s = dot(normal, aabb.center) + d;
    return s + r >= 0.0;
}

bool aabb_vs_frustum(AABB aabb, Frustum f)
{
    for (int i = 0; i < 6; i++)
    {
        if (!aabb_vs_plane(aabb, f.planes[i]))
            return false;
    }
    return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    uint idx = tid.x;
    if (idx >= params.object_count)
        return;

    Frustum f = frustum_buf[0];
    AABB aabb = bounds[idx];
    bool visible = aabb_vs_frustum(aabb, f);

    uint word_index = idx / 32;
    uint bit_index = idx % 32;

    if (visible)
        InterlockedOr(visibility[word_index], 1u << bit_index);
}
