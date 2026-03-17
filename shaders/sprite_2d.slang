struct Transform2D
{
    float2 pos;
    float2 scale;
    float rotation;
    float depth;
    uint flags;
    uint _pad;
};

struct SpriteInfo
{
    float4 uv;
    float4 color;
    uint texture_idx;
    uint material_base_id;
    uint layer;
    uint _pad;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    uint texture_idx : TEXCOORD1;
};

static const uint RF2D_HIDDEN = 1 << 0;
static const uint RF2D_FLIP_X = 1 << 1;
static const uint RF2D_FLIP_Y = 1 << 2;

struct PushConstants
{
    float4x4 projection;
};

[[vk::push_constant]]
PushConstants push;

[[vk::binding(0, 0)]]
StructuredBuffer<Transform2D> transforms;

[[vk::binding(1, 0)]]
StructuredBuffer<SpriteInfo> sprite_infos;

[[vk::binding(0, 1)]]
Sampler2D textures[];

static const float2 QUAD_VERTS[4] = {
    float2(-0.5, -0.5),
    float2( 0.5, -0.5),
    float2( 0.5,  0.5),
    float2(-0.5,  0.5),
};

static const uint QUAD_INDICES[6] = { 0, 1, 2, 0, 2, 3 };

[shader("vertex")]
VSOutput vertexMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    VSOutput output;

    Transform2D t = transforms[instance_id];
    SpriteInfo s = sprite_infos[instance_id];

    if (t.flags & RF2D_HIDDEN)
    {
        output.position = float4(0.0, 0.0, 0.0, 0.0);
        output.uv = float2(0.0, 0.0);
        output.color = float4(0.0, 0.0, 0.0, 0.0);
        output.texture_idx = 0;
        return output;
    }

    uint vi = QUAD_INDICES[vertex_id];
    float2 local = QUAD_VERTS[vi];

    float2 scaled = local * t.scale;

    float cs = cos(t.rotation);
    float sn = sin(t.rotation);
    float2 rotated = float2(
        scaled.x * cs - scaled.y * sn,
        scaled.x * sn + scaled.y * cs
    );

    float2 world = rotated + t.pos;

    output.position = mul(push.projection, float4(world, t.depth, 1.0));

    float2 uv_origin = s.uv.xy;
    float2 uv_size = s.uv.zw;

    float2 uv_local = local + float2(0.5, 0.5);

    if (t.flags & RF2D_FLIP_X)
        uv_local.x = 1.0 - uv_local.x;

    if (t.flags & RF2D_FLIP_Y)
        uv_local.y = 1.0 - uv_local.y;

    output.uv = uv_origin + uv_local * uv_size;
    output.color = s.color;
    output.texture_idx = s.texture_idx;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float4 texColor = textures[NonUniformResourceIndex(input.texture_idx)].Sample(input.uv);
    float4 result = texColor * input.color;

    if (result.a < 0.01)
        discard;

    return result;
}
