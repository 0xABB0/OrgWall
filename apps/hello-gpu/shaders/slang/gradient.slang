struct VOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VOut vs_main(uint vid : SV_VertexID)
{
    VOut o;
    float2 p = float2(float((vid << 1) & 2), float(vid & 2));
    o.uv = p;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fs_main(VOut input) : SV_Target
{
    float3 top = float3(0.05, 0.07, 0.16);
    float3 bottom = float3(0.18, 0.10, 0.22);
    float3 c = lerp(top, bottom, input.uv.y);
    c += 0.05 * float3(input.uv.x, 0.5 * input.uv.x, 1.0 - input.uv.x);
    return float4(c, 1.0);
}
