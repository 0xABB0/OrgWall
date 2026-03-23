struct ResponseVSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct ResponseExposurePush
{
    float multiplier;
    float3 _pad;
};

static const float2 QUAD_POS[6] = {
    float2(-1, -1), float2( 1, -1), float2( 1,  1),
    float2(-1, -1), float2( 1,  1), float2(-1,  1),
};

static const float2 QUAD_UV[6] = {
    float2(0, 0), float2(1, 0), float2(1, 1),
    float2(0, 0), float2(1, 1), float2(0, 1),
};

[[vk::push_constant]]
ConstantBuffer<ResponseExposurePush> response_push;

[[vk::binding(0, 0)]]
Sampler2D source_texture;

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
    return source_texture.Sample(input.uv) * response_push.multiplier;
}
