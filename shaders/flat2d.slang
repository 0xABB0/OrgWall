struct Push {
    float4x4 projection;
};

[[vk::push_constant]] Push push;

struct VIn {
    float2 pos   : POSITION;
    float4 color : COLOR;
};

struct VOut {
    float4 pos   : SV_Position;
    float4 color : COLOR;
};

[shader("vertex")]
VOut vertexMain(VIn input)
{
    VOut output;
    output.pos = mul(push.projection, float4(input.pos, 0.0, 1.0));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VOut input) : SV_Target
{
    return input.color;
}
