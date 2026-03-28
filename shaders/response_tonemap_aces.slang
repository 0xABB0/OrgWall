import response_fullscreen;

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
    return fullscreenVertex(vid);
}

[shader("fragment")]
float4 fragmentMain(ResponseVSOutput input) : SV_Target
{
    float4 src = source_texture.Sample(input.uv);
    return float4(aces_tonemap(src.rgb), src.a);
}
