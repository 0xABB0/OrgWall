struct TextSDFParams
{
    float4 color;
    float4 outline_color;
    float edge;
    float softness;
    float outline;
    float px_range;
};

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

struct TextSDFVSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    uint texture_idx : TEXCOORD1;
    uint material_idx : TEXCOORD2;
};

static const uint RF2D_HIDDEN = 1 << 0;
static const uint RF2D_FLIP_X = 1 << 1;
static const uint RF2D_FLIP_Y = 1 << 2;

struct TextSDFPush
{
    float4x4 projection;
    uint draw_offset;
};

[[vk::push_constant]]
TextSDFPush push;

[[vk::binding(0, 0)]]
StructuredBuffer<Transform2D> transforms;

[[vk::binding(1, 0)]]
StructuredBuffer<SpriteInfo> sprite_infos;

[[vk::binding(2, 0)]]
StructuredBuffer<TextSDFParams> materials;

[[vk::binding(3, 0)]]
StructuredBuffer<uint> draw_order;

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
TextSDFVSOutput vertexMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    TextSDFVSOutput output;

    uint slot = draw_order[push.draw_offset + instance_id];
    Transform2D t = transforms[slot];
    SpriteInfo s = sprite_infos[slot];

    if ((t.flags & RF2D_HIDDEN) != 0)
    {
        output.position = float4(0, 0, 0, 0);
        output.uv = float2(0, 0);
        output.color = float4(0, 0, 0, 0);
        output.texture_idx = 0;
        output.material_idx = 0;
        return output;
    }

    uint vi = QUAD_INDICES[vertex_id];
    float2 local = QUAD_VERTS[vi];
    float2 scaled = local * t.scale;

    float cs = cos(t.rotation);
    float sn = sin(t.rotation);
    float2 rotated = float2(scaled.x * cs - scaled.y * sn, scaled.x * sn + scaled.y * cs);
    float2 world = rotated + t.pos;

    output.position = mul(push.projection, float4(world, t.depth, 1.0));

    float2 uv_local = local + float2(0.5, 0.5);
    if ((t.flags & RF2D_FLIP_X) != 0) uv_local.x = 1.0 - uv_local.x;
    if ((t.flags & RF2D_FLIP_Y) != 0) uv_local.y = 1.0 - uv_local.y;

    output.uv = s.uv.xy + uv_local * s.uv.zw;
    output.color = s.color;
    output.texture_idx = s.texture_idx;
    output.material_idx = s.layer;

    return output;
}

[shader("fragment")]
float4 fragmentMain(TextSDFVSOutput input) : SV_Target
{
    TextSDFParams mat = materials[input.material_idx];

    float distance = textures[NonUniformResourceIndex(input.texture_idx)].Sample(input.uv).r;

    float edge = mat.edge;
    float soft = mat.softness;
    float outline_width = mat.outline;

    float alpha = smoothstep(edge - soft, edge + soft, distance);

    float4 fill_color = mat.color * input.color;

    if (outline_width > 0.0)
    {
        float outline_edge = edge - outline_width;
        float outline_alpha = smoothstep(outline_edge - soft, outline_edge + soft, distance);
        float4 result = lerp(mat.outline_color, fill_color, alpha);
        result.a *= outline_alpha;
        if (result.a < 0.01)
            discard;
        return result;
    }

    fill_color.a *= alpha;
    if (fill_color.a < 0.01)
        discard;

    return fill_color;
}
