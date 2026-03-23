struct ResponseVSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

static const float2 QUAD_POS[6] = {
    float2(-1, -1), float2( 1, -1), float2( 1,  1),
    float2(-1, -1), float2( 1,  1), float2(-1,  1),
};

static const float2 QUAD_UV[6] = {
    float2(0, 0), float2(1, 0), float2(1, 1),
    float2(0, 0), float2(1, 1), float2(0, 1),
};

[[vk::binding(0, 0)]]
Sampler2D source_texture;

float3 aces_tonemap(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[shader("vertex")]
ResponseVSOutput vertexMain(uint vid : SV_VertexID)
{
    ResponseVSOutput o;
    o.position = float4(QUAD_POS[vid], 0.0, 1.0);
    o.uv = QUAD_UV[vid];
    return o;
}

[shader("fragment")]
float4 fragmentMain(ResponseVSOutput input) : SV_Target
{
    float4 src = source_texture.Sample(input.uv);
    return float4(aces_tonemap(src.rgb), src.a);
}
