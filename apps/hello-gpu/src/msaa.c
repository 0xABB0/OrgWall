#include <math.h>
#include <stdlib.h>

#include <log/log.h>

#include "msaa.h"
#include "hud.h"

static const char MSAA_SLANG[] = {
#embed "shaders/slang/msaa.slang"
    , 0
};

#define STAR_SPOKES 11
#define STAR_VERTS  (STAR_SPOKES * 3)
#define VBO_FRAMES  3

static inline bool handle_zero(Mel_SlotMap_Handle h) { return h.index == 0 && h.generation == 0; }

typedef struct
{
    f32 pos[2];
    f32 color[4];
} Star_Vertex;

typedef struct
{
    u32 resolved;
    u32 reference;
    u32 smp;
    f32 angle;
    f32 aspect;
    f32 pad0;
    f32 pad1;
} Msaa_Root;

typedef struct
{
    Mel_Gpu_Device* dev;
    bool            ready;
    u32             samples;

    Mel_Gpu_Shader   ms_shader;
    Mel_Gpu_Pipeline ms_pipeline;
    Mel_Gpu_Shader   ref_shader;
    Mel_Gpu_Pipeline ref_pipeline;
    Mel_Gpu_Buffer   vbo[VBO_FRAMES];
    i32              vframe;

    Mel_Gpu_Shader   compose_shader;
    Mel_Gpu_Pipeline compose_pipeline;
    Mel_Gpu_Sampler  sampler;

    i32                  tw, th;
    Mel_Gpu_Texture      ms;
    Mel_Gpu_Texture_View ms_view;
    Mel_Gpu_Texture      aa;
    Mel_Gpu_Texture_View aa_view;
    u32                  aa_slot;
    Mel_Gpu_Texture      ref;
    Mel_Gpu_Texture_View ref_view;
    u32                  ref_slot;
    bool                 targets_fresh;

    Hud hud;
    f64 angle;
} Msaa;

static u32 pick_samples(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Format_Properties fp = mel_gpu_format_properties(dev, MEL_GPU_FORMAT_RGBA8_UNORM, MEL_GPU_TILING_OPTIMAL);
    if (fp.sample_counts & 4u)
        return 4;
    if (fp.sample_counts & 2u)
        return 2;
    return 1;
}

static void destroy_targets(Msaa* m)
{
    if (!handle_zero(m->ms_view.slot))
        mel_gpu_texture_view_destroy(m->dev, m->ms_view);
    if (!handle_zero(m->ms.slot))
        mel_gpu_texture_destroy(m->dev, m->ms);
    if (!handle_zero(m->aa_view.slot))
        mel_gpu_texture_view_destroy(m->dev, m->aa_view);
    if (!handle_zero(m->aa.slot))
        mel_gpu_texture_destroy(m->dev, m->aa);
    if (!handle_zero(m->ref_view.slot))
        mel_gpu_texture_view_destroy(m->dev, m->ref_view);
    if (!handle_zero(m->ref.slot))
        mel_gpu_texture_destroy(m->dev, m->ref);
    m->ms_view = m->aa_view = m->ref_view = (Mel_Gpu_Texture_View){ 0 };
    m->ms = m->aa = m->ref = (Mel_Gpu_Texture){ 0 };
}

static void* msaa_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    (void)sc;
    Msaa* m = calloc(1, sizeof *m);
    m->dev = dev;
    hud_init(&m->hud, dev);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "msaa: bindless heap unavailable; cannot present the resolved targets");
        return m;
    }

    m->samples = pick_samples(dev);
    if (m->samples < 2)
        mel_log_warn("hello-gpu", "msaa: device offers no multisample RGBA8; both halves render single-sampled");

    Mel_Gpu_Pipeline_From_Slang_Result ms = mel_gpu_pipeline_create_from_slang(dev,
                                                                               .source = MSAA_SLANG,
                                                                               .vertex_entry = "vs_star",
                                                                               .fragment_entry = "fs_star",
                                                                               .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                               .cull = MEL_GPU_CULL_NONE,
                                                                               .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                               .samples = m->samples,
                                                                               .bindless = true,
                                                                               .name = "star-msaa");
    if (mel_gpu_failed(ms.status))
        return m;
    m->ms_shader = ms.shader;
    m->ms_pipeline = ms.value;

    Mel_Gpu_Pipeline_From_Slang_Result ref = mel_gpu_pipeline_create_from_slang(dev,
                                                                                .source = MSAA_SLANG,
                                                                                .vertex_entry = "vs_star",
                                                                                .fragment_entry = "fs_star",
                                                                                .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                                .cull = MEL_GPU_CULL_NONE,
                                                                                .color_format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                                .samples = 1,
                                                                                .bindless = true,
                                                                                .name = "star-ref");
    if (mel_gpu_failed(ref.status))
        return m;
    m->ref_shader = ref.shader;
    m->ref_pipeline = ref.value;

    for (i32 i = 0; i < VBO_FRAMES; ++i)
        m->vbo[i] = mel_gpu_buffer_create(dev, .size = STAR_VERTS * sizeof(Star_Vertex), .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "star-vbo").value;

    Mel_Gpu_Pipeline_From_Slang_Result compose = mel_gpu_pipeline_create_from_slang(dev,
                                                                                    .source = MSAA_SLANG,
                                                                                    .vertex_entry = "vs_compose",
                                                                                    .fragment_entry = "fs_compose",
                                                                                    .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                                    .cull = MEL_GPU_CULL_NONE,
                                                                                    .color_format = mel_gpu_swapchain_format(sc),
                                                                                    .bindless = true,
                                                                                    .name = "msaa-compose");
    if (mel_gpu_failed(compose.status))
        return m;
    m->compose_shader = compose.shader;
    m->compose_pipeline = compose.value;

    m->sampler = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "msaa-sampler").value;

    m->ready = true;
    return m;
}

static void msaa_resize(void* state, i32 w, i32 h)
{
    Msaa* m = state;
    if (!m->ready)
        return;

    i32 tw = w / 2, th = h;
    if (tw < 1)
        tw = 1;
    if (th < 1)
        th = 1;
    if (tw == m->tw && th == m->th && !handle_zero(m->aa.slot))
        return;

    destroy_targets(m);
    m->tw = tw;
    m->th = th;

    if (m->samples >= 2)
    {
        m->ms = mel_gpu_texture_create(m->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)tw, (u32)th, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .sample_count = m->samples, .usage = MEL_GPU_TEXTURE_ATTACHMENT, .name = "msaa-ms").value;
        m->ms_view = mel_gpu_texture_default_view(m->dev, m->ms).value;
    }

    m->aa = mel_gpu_texture_create(m->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)tw, (u32)th, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_SAMPLED, .name = "msaa-resolved").value;
    m->aa_view = mel_gpu_texture_default_view(m->dev, m->aa).value;
    m->aa_slot = mel_gpu_texture_view_bindless_slot(m->dev, m->aa_view);

    m->ref = mel_gpu_texture_create(m->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { (u32)tw, (u32)th, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_SAMPLED, .name = "msaa-ref").value;
    m->ref_view = mel_gpu_texture_default_view(m->dev, m->ref).value;
    m->ref_slot = mel_gpu_texture_view_bindless_slot(m->dev, m->ref_view);

    m->targets_fresh = true;
}

static u32 build_star(Star_Vertex* out)
{
    const f32 tip = 0.85f, valley = 0.32f;
    u32       n = 0;
    for (i32 s = 0; s < STAR_SPOKES; ++s)
    {
        f32 a0 = (f32)s / (f32)STAR_SPOKES * 6.2831853f;
        f32 a1 = (f32)(s + 1) / (f32)STAR_SPOKES * 6.2831853f;
        f32 am = (a0 + a1) * 0.5f;
        f32 hue = (f32)s / (f32)STAR_SPOKES;
        f32 cr = 0.5f + 0.5f * cosf(6.2831f * (hue + 0.00f));
        f32 cg = 0.5f + 0.5f * cosf(6.2831f * (hue + 0.33f));
        f32 cb = 0.5f + 0.5f * cosf(6.2831f * (hue + 0.67f));

        out[n++] = (Star_Vertex){ { 0.0f, 0.0f }, { cr, cg, cb, 1.0f } };
        out[n++] = (Star_Vertex){ { tip * cosf(am), tip * sinf(am) }, { cr, cg, cb, 1.0f } };
        out[n++] = (Star_Vertex){ { valley * cosf(a1), valley * sinf(a1) }, { cr * 0.5f, cg * 0.5f, cb * 0.5f, 1.0f } };
    }
    return n;
}

static void msaa_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Msaa* m = state;
    m->angle += dt * 0.5;
    hud_frame(&m->hud, dt, m->samples >= 2 ? (m->samples == 4 ? "MSAA 4× | left resolved, right 1×" : "MSAA 2× | left resolved, right 1×") : "MSAA unavailable (1×)");

    if (!m->ready || handle_zero(m->aa.slot))
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    Star_Vertex verts[STAR_VERTS];
    u32         count = build_star(verts);
    m->vframe = (m->vframe + 1) % VBO_FRAMES;
    Mel_Gpu_Buffer vbo = m->vbo[m->vframe];
    mel_gpu_buffer_write(m->dev, vbo, verts, count * sizeof(Star_Vertex));

    Msaa_Root root = {
        .resolved = m->aa_slot,
        .reference = m->ref_slot,
        .smp = mel_gpu_sampler_bindless_slot(m->dev, m->sampler),
        .angle = (f32)m->angle,
        .aspect = (f32)m->tw / (f32)m->th,
    };

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
    bool                      fresh = m->targets_fresh;
    Mel_Gpu_Resource_State    src = fresh ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;
    m->targets_fresh = false;

    mel_gpu_cmd_texture_barrier(cmd, m->aa, range, src, MEL_GPU_STATE_RENDER_TARGET);
    if (m->samples >= 2)
    {
        Mel_Gpu_Resource_State ms_src = fresh ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_RENDER_TARGET;
        mel_gpu_cmd_texture_barrier(cmd, m->ms, range, ms_src, MEL_GPU_STATE_RENDER_TARGET);
        Mel_Gpu_Color_Attachment color = { .view = m->ms_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_DONT_CARE, .clear = mel_gpu_rgba(0.05f, 0.06f, 0.09f, 1.0f), .resolve_view = m->aa_view };
        mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = (u32)m->tw, .height = (u32)m->th);
        mel_gpu_cmd_bind_pipeline(cmd, m->ms_pipeline);
        mel_gpu_cmd_bind_bindless(cmd);
        mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
        mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo);
        mel_gpu_cmd_draw(cmd, count, 1);
        mel_gpu_cmd_end_rendering(cmd);
    }
    else
    {
        Mel_Gpu_Color_Attachment color = { .view = m->aa_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.05f, 0.06f, 0.09f, 1.0f) };
        mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = (u32)m->tw, .height = (u32)m->th);
        mel_gpu_cmd_bind_pipeline(cmd, m->ref_pipeline);
        mel_gpu_cmd_bind_bindless(cmd);
        mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
        mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo);
        mel_gpu_cmd_draw(cmd, count, 1);
        mel_gpu_cmd_end_rendering(cmd);
    }
    mel_gpu_cmd_texture_barrier(cmd, m->aa, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_SHADER_RESOURCE);

    mel_gpu_cmd_texture_barrier(cmd, m->ref, range, src, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment refc = { .view = m->ref_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0.05f, 0.06f, 0.09f, 1.0f) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &refc, .color_count = 1, .width = (u32)m->tw, .height = (u32)m->th);
    mel_gpu_cmd_bind_pipeline(cmd, m->ref_pipeline);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, vbo);
    mel_gpu_cmd_draw(cmd, count, 1);
    mel_gpu_cmd_end_rendering(cmd);
    mel_gpu_cmd_texture_barrier(cmd, m->ref, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_SHADER_RESOURCE);

    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0, 0, 0, 1));
    mel_gpu_cmd_bind_pipeline(cmd, m->compose_pipeline);
    mel_gpu_cmd_bind_bindless(cmd);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_pass(cmd);
}

static void msaa_teardown(void* state)
{
    Msaa* m = state;
    if (!m)
        return;
    if (m->ready)
    {
        destroy_targets(m);
        mel_gpu_sampler_destroy(m->dev, m->sampler);
        mel_gpu_pipeline_destroy(m->dev, m->compose_pipeline);
        mel_gpu_shader_destroy(m->dev, m->compose_shader);
        for (i32 i = 0; i < VBO_FRAMES; ++i)
            mel_gpu_buffer_destroy(m->dev, m->vbo[i]);
        mel_gpu_pipeline_destroy(m->dev, m->ref_pipeline);
        mel_gpu_shader_destroy(m->dev, m->ref_shader);
        mel_gpu_pipeline_destroy(m->dev, m->ms_pipeline);
        mel_gpu_shader_destroy(m->dev, m->ms_shader);
    }
    free(m);
}

const Graphical_App MSAA_APP = {
    .title = "msaa-resolve",
    .init = msaa_init,
    .resize = msaa_resize,
    .render = msaa_render,
    .teardown = msaa_teardown,
};
