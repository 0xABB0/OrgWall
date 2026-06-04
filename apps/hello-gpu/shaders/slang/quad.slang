struct Root
{
    float4 rect;
    float4 color;
};

[[vk::push_constant]]
Root root;

struct VOut
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
VOut vs_main(uint vid : SV_VertexID)
{
    const float2 corners[6] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
        float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
    };

    VOut o;
    float2 ndc = root.rect.xy + corners[vid] * root.rect.zw;
    o.color = root.color;
    o.pos = float4(ndc, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fs_main(VOut input) : SV_Target
{
    return input.color;
}
