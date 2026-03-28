import response_fullscreen;

struct ResponseExposurePush
{
    float multiplier;
    float3 _pad;
};

[[vk::push_constant]]
ConstantBuffer<ResponseExposurePush> response_push;

[[vk::binding(0, 0)]]
Sampler2D source_texture;

[shader("vertex")]
ResponseVSOutput vertexMain(uint vid : SV_VertexID)
{
    return fullscreenVertex(vid);
}

[shader("fragment")]
float4 fragmentMain(ResponseVSOutput input) : SV_Target
{
    return source_texture.Sample(input.uv) * response_push.multiplier;
}
