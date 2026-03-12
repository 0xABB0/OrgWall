struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct PushConstants
{
    float4x4 projection;
};

[[vk::push_constant]]
PushConstants push;

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    output.position = mul(push.projection, float4(input.position, 0.0, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[[vk::binding(0, 0)]]
Sampler2D spriteTexture;

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float4 texColor = spriteTexture.Sample(input.uv);
    return texColor * input.color;
}
