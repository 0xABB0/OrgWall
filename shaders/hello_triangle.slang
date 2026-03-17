struct VSOutput
{
    float4 position : SV_Position;
    float4 color    : COLOR0;
};

struct PushConstants
{
    float4x4 projection;
};

[[vk::push_constant]]
PushConstants push;

static const float2 POSITIONS[3] = {
    float2( 0.0,  0.5),
    float2( 0.5, -0.5),
    float2(-0.5, -0.5),
};

static const float4 COLORS[3] = {
    float4(1.0, 0.0, 0.0, 1.0),
    float4(0.0, 1.0, 0.0, 1.0),
    float4(0.0, 0.0, 1.0, 1.0),
};

[shader("vertex")]
VSOutput vertexMain(uint vertex_id : SV_VertexID)
{
    VSOutput out;
    out.position = mul(push.projection, float4(POSITIONS[vertex_id], 0.0, 1.0));
    out.color    = COLORS[vertex_id];
    return out;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return input.color;
}
