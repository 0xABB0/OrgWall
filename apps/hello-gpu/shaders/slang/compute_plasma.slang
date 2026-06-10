#if defined(MEL_TARGET_METAL)

struct Root
{
    DescriptorHandle<RWStructuredBuffer<float4> > cells_rw;
    DescriptorHandle<StructuredBuffer<float4> > cells_ro;
    uint grid_w;
    uint grid_h;
    float time;
    float pad;
};

[[vk::push_constant]]
Root root;

RWStructuredBuffer<float4> mel_cells_rw() { return root.cells_rw; }
StructuredBuffer<float4> mel_cells_ro() { return root.cells_ro; }

#else

struct Root
{
    uint cells_rw;
    uint cells_ro;
    uint grid_w;
    uint grid_h;
    float time;
    float pad;
};

[[vk::push_constant]]
Root root;

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> u_rw_buffers[];

[[vk::binding(0, 2)]]
StructuredBuffer<float4> u_ro_buffers[];

RWStructuredBuffer<float4> mel_cells_rw() { return u_rw_buffers[root.cells_rw]; }
StructuredBuffer<float4> mel_cells_ro() { return u_ro_buffers[NonUniformResourceIndex(root.cells_ro)]; }

#endif

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint x = tid.x;
    uint y = tid.y;
    if (x >= root.grid_w || y >= root.grid_h)
        return;

    float fx = float(x) / float(root.grid_w);
    float fy = float(y) / float(root.grid_h);
    float t = root.time;

    float v = sin(fx * 10.0 + t)
            + sin((fy * 10.0 + t) * 0.8)
            + sin((fx + fy) * 8.0 + t * 1.3)
            + sin(sqrt((fx - 0.5) * (fx - 0.5) + (fy - 0.5) * (fy - 0.5)) * 24.0 - t * 2.0);
    v *= 0.25;

    float3 col = 0.5 + 0.5 * float3(
        sin(v * 3.14159 + 0.0),
        sin(v * 3.14159 + 2.094),
        sin(v * 3.14159 + 4.188));

    uint idx = y * root.grid_w + x;
    mel_cells_rw()[idx] = float4(col, 1.0);
}

struct VOut
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
VOut vs_cells(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    const float2 corners[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
        float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0),
    };

    uint cx = iid % root.grid_w;
    uint cy = iid / root.grid_w;

    float2 cell = float2(1.0 / float(root.grid_w), 1.0 / float(root.grid_h));
    float2 uv = (float2(float(cx), float(cy)) + corners[vid] * 0.96) * cell;
    float2 ndc = uv * 2.0 - 1.0;

    VOut o;
    o.color = mel_cells_ro()[iid];
    o.pos = float4(ndc, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fs_cells(VOut input) : SV_Target
{
    return input.color;
}
