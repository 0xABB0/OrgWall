#if defined(MEL_TARGET_METAL)

struct Root
{
    DescriptorHandle<Texture2D<float4> > shadow_tex;
    DescriptorHandle<SamplerState> shadow_smp;
    float depth_bias;
    float pad1;
    float4 light_dir;
};

[[vk::push_constant]]
Root root;

Texture2D<float4> mel_shadow_tex() { return root.shadow_tex; }
SamplerState mel_shadow_smp() { return root.shadow_smp; }

#else

struct Root
{
    uint shadow_tex;
    uint shadow_smp;
    float depth_bias;
    float pad1;
    float4 light_dir;
};

[[vk::push_constant]]
Root root;

[[vk::binding(0, 0)]]
Texture2D<float4> u_textures[];

[[vk::binding(1, 0)]]
SamplerState u_samplers[];

Texture2D<float4> mel_shadow_tex() { return u_textures[NonUniformResourceIndex(root.shadow_tex)]; }
SamplerState mel_shadow_smp() { return u_samplers[NonUniformResourceIndex(root.shadow_smp)]; }

#endif

struct Depth_VIn
{
    float3 pos : POSITION;
};

struct Depth_VOut
{
    float4 pos : SV_Position;
};

[shader("vertex")]
Depth_VOut vs_depth(Depth_VIn input)
{
    Depth_VOut o;
    o.pos = float4(input.pos.xy, input.pos.z + root.depth_bias, 1.0);
    return o;
}

[shader("fragment")]
void fs_depth(Depth_VOut input)
{
}

struct Scene_VIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR0;
    float3 shadow_uvz : TEXCOORD0;
};

struct Scene_VOut
{
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    float3 color : COLOR0;
    float3 shadow_uvz : TEXCOORD0;
};

[shader("vertex")]
Scene_VOut vs_scene(Scene_VIn input)
{
    Scene_VOut o;
    o.pos = float4(input.pos, 1.0);
    o.normal = input.normal;
    o.color = input.color;
    o.shadow_uvz = input.shadow_uvz;
    return o;
}

[shader("fragment")]
float4 fs_scene(Scene_VOut input) : SV_Target
{
    float3 n = normalize(input.normal);
    float3 ld = normalize(-root.light_dir.xyz);
    float ndotl = max(dot(n, ld), 0.0);

    float2 suv = input.shadow_uvz.xy;
    float sz = input.shadow_uvz.z;
    float sd = mel_shadow_tex().Sample(mel_shadow_smp(), suv).r;
    float lit = (sz - 0.003 <= sd) ? 1.0 : 0.2;

    float3 col = input.color * (0.15 + 0.85 * ndotl * lit);
    return float4(col, 1.0);
}
