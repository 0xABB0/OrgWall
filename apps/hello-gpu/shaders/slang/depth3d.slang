struct VIn
{
    float3 pos : POSITION;
    float4 color : COLOR0;
};

struct VOut
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
VOut vs_scene(VIn input)
{
    VOut o;
    o.pos = float4(input.pos, 1.0);
    o.color = input.color;
    return o;
}

[shader("fragment")]
float4 fs_scene(VOut input) : SV_Target
{
    return input.color;
}
