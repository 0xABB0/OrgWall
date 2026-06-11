struct VIn
{
    float2 pos : POSITION;
    float3 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VOut
{
    float4 pos : SV_Position;
    float3 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VOut vs_main(VIn input)
{
    VOut o;
    o.pos = float4(input.pos, 0.0, 1.0);
    o.uv = input.uv;
    o.color = input.color;
    return o;
}

[shader("fragment")]
float4 fs_main(VOut input) : SV_Target
{
    float r = length(input.uv.xy);
    float a = input.color.a * (1.0 - smoothstep(1.0 - input.uv.z, 1.0, r));
    return float4(input.color.rgb, a);
}
