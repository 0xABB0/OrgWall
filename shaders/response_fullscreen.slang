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

ResponseVSOutput fullscreenVertex(uint vid)
{
    ResponseVSOutput o;
    o.position = float4(QUAD_POS[vid], 0.0, 1.0);
    o.uv = QUAD_UV[vid];
    return o;
}
