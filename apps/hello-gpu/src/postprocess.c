#include <math.h>
#include <stdlib.h>

#include <log/log.h>

#include "postprocess.h"
#include "quad_spv.h"
#include "gradient_spv.h"
#include "post_spv.h"
#include "blit_spv.h"

#define OFF_W       1024
#define OFF_H       768

#define SCENE_QUADS 6

typedef struct
{
    f32 rect[4];
    f32 color[4];
} Quad_Root;

typedef struct
{
    u32 tex;
    u32 smp;
    f32 amount;
    f32 vignette;
} Post_Root;

typedef struct
{
    Mel_Gpu_Device*      dev;
    bool                 ready;
    Mel_Gpu_Texture      scene;
    Mel_Gpu_Texture_View scene_view;
    u32                  scene_slot;
    Mel_Gpu_Shader       bg_shader;
    Mel_Gpu_Pipeline     bg_pipeline;
    Mel_Gpu_Shader       quad_shader;
    Mel_Gpu_Pipeline     quad_pipeline;
    Mel_Gpu_Shader       post_shader;
    Mel_Gpu_Pipeline     post_pipeline;
    Mel_Gpu_Sampler      sampler;
    bool                 first_frame;
    f64                  t;
} Post;

static void* post_init(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc)
{
    Post* p = calloc(1, sizeof *p);
    p->dev = dev;
    Mel_Gpu_Format fmt = mel_gpu_swapchain_format(sc);

    if (!mel_gpu_bindless_available(dev))
    {
        mel_log_warn("hello-gpu", "postprocess: bindless heap unavailable; cannot sample the offscreen scene");
        return p;
    }

    Mel_Gpu_Texture_Create_Result scene = mel_gpu_texture_create(dev,
                                                                 .kind = MEL_GPU_TEXTURE_2D,
                                                                 .extent = { OFF_W, OFF_H, 1 },
                                                                 .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                 .usage = MEL_GPU_TEXTURE_ATTACHMENT | MEL_GPU_TEXTURE_SAMPLED,
                                                                 .name = "scene");
    if (mel_gpu_failed(scene.status))
        return p;
    p->scene = scene.value;
    p->scene_view = mel_gpu_texture_default_view(dev, p->scene).value;
    p->scene_slot = mel_gpu_texture_view_bindless_slot(dev, p->scene_view);

    p->bg_shader = mel_gpu_shader_create_from_bytecode(dev,
                                                       .spirv_vertex = BLIT_VERT_SPV,
                                                       .spirv_vertex_size = sizeof BLIT_VERT_SPV,
                                                       .spirv_fragment = GRADIENT_FRAG_SPV,
                                                       .spirv_fragment_size = sizeof GRADIENT_FRAG_SPV,
                                                       .vertex_entry = "main",
                                                       .fragment_entry = "main",
                                                       .name = "scene-bg")
                       .value;
    p->bg_pipeline = mel_gpu_pipeline_create(dev, .shader = p->bg_shader, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "scene-bg").value;

    p->quad_shader = mel_gpu_shader_create_from_bytecode(dev,
                                                         .spirv_vertex = QUAD_VERT_SPV,
                                                         .spirv_vertex_size = sizeof QUAD_VERT_SPV,
                                                         .spirv_fragment = QUAD_FRAG_SPV,
                                                         .spirv_fragment_size = sizeof QUAD_FRAG_SPV,
                                                         .vertex_entry = "main",
                                                         .fragment_entry = "main",
                                                         .name = "scene-quad")
                         .value;
    Mel_Gpu_Color_Target target = { .format = MEL_GPU_FORMAT_RGBA8_UNORM, .blend = MEL_GPU_BLEND_ALPHA };
    p->quad_pipeline = mel_gpu_pipeline_create(dev,
                                               .shader = p->quad_shader,
                                               .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                               .cull = MEL_GPU_CULL_NONE,
                                               .color_targets = &target,
                                               .color_target_count = 1,
                                               .push_constant_size = sizeof(Quad_Root),
                                               .name = "scene-quad")
                           .value;

    p->post_shader = mel_gpu_shader_create_from_bytecode(dev,
                                                         .spirv_vertex = BLIT_VERT_SPV,
                                                         .spirv_vertex_size = sizeof BLIT_VERT_SPV,
                                                         .spirv_fragment = POST_FRAG_SPV,
                                                         .spirv_fragment_size = sizeof POST_FRAG_SPV,
                                                         .vertex_entry = "main",
                                                         .fragment_entry = "main",
                                                         .name = "post")
                         .value;
    p->post_pipeline = mel_gpu_pipeline_create(dev, .shader = p->post_shader, .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST, .cull = MEL_GPU_CULL_NONE, .color_format = fmt, .name = "post").value;

    Mel_Gpu_Sampler_Create_Result smp = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR, .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "post-sampler");
    if (mel_gpu_failed(smp.status))
        return p;
    p->sampler = smp.value;

    p->first_frame = true;
    p->ready = true;
    return p;
}

static void post_render(void* state, Mel_Gpu_Command_List* cmd, f64 dt)
{
    Post* p = state;
    p->t += dt;

    if (!p->ready)
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.20f, 0.10f, 0.02f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        return;
    }

    Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };

    Mel_Gpu_Resource_State scene_src = p->first_frame ? MEL_GPU_STATE_COMMON : MEL_GPU_STATE_SHADER_RESOURCE;
    p->first_frame = false;

    mel_gpu_cmd_texture_barrier(cmd, p->scene, range, scene_src, MEL_GPU_STATE_RENDER_TARGET);
    Mel_Gpu_Color_Attachment color = { .view = p->scene_view, .load = MEL_GPU_LOAD_CLEAR, .store = MEL_GPU_STORE_STORE, .clear = mel_gpu_rgba(0, 0, 0, 1) };
    mel_gpu_cmd_begin_rendering(cmd, .colors = &color, .color_count = 1, .width = OFF_W, .height = OFF_H);

    mel_gpu_cmd_bind_pipeline(cmd, p->bg_pipeline);
    mel_gpu_cmd_draw(cmd, 3, 1);

    mel_gpu_cmd_bind_pipeline(cmd, p->quad_pipeline);
    static const f32 hue[SCENE_QUADS][3] = {
        { 0.95f, 0.26f, 0.21f }, { 0.18f, 0.80f, 0.44f }, { 0.20f, 0.60f, 0.86f }, { 0.95f, 0.77f, 0.06f }, { 0.61f, 0.35f, 0.71f }, { 0.10f, 0.74f, 0.71f },
    };
    for (i32 i = 0; i < SCENE_QUADS; ++i)
    {
        f32       phase = (f32)i / (f32)SCENE_QUADS * 6.2831853f;
        f32       drift = (f32)p->t * 0.7f + phase;
        Quad_Root q = {
            .rect = { 0.55f * cosf(drift), 0.55f * sinf(drift * 0.9f), 0.22f, 0.22f },
            .color = { hue[i][0], hue[i][1], hue[i][2], 0.6f },
        };
        mel_gpu_cmd_push_constants(cmd, 0, sizeof q, &q);
        mel_gpu_cmd_draw(cmd, 6, 1);
    }
    mel_gpu_cmd_end_rendering(cmd);

    mel_gpu_cmd_texture_barrier(cmd, p->scene, range, MEL_GPU_STATE_RENDER_TARGET, MEL_GPU_STATE_SHADER_RESOURCE);

    Post_Root root = {
        .tex = p->scene_slot,
        .smp = mel_gpu_sampler_bindless_slot(p->dev, p->sampler),
        .amount = 0.35f + 0.30f * (f32)sin(p->t * 1.5),
        .vignette = 0.85f,
    };
    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0, 0, 0, 1));
    mel_gpu_cmd_bind_pipeline(cmd, p->post_pipeline);
    mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
    mel_gpu_cmd_draw(cmd, 3, 1);
    mel_gpu_cmd_end_pass(cmd);
}

static void post_teardown(void* state)
{
    Post* p = state;
    if (!p)
        return;
    if (p->ready)
    {
        mel_gpu_sampler_destroy(p->dev, p->sampler);
        mel_gpu_pipeline_destroy(p->dev, p->post_pipeline);
        mel_gpu_shader_destroy(p->dev, p->post_shader);
        mel_gpu_pipeline_destroy(p->dev, p->quad_pipeline);
        mel_gpu_shader_destroy(p->dev, p->quad_shader);
        mel_gpu_pipeline_destroy(p->dev, p->bg_pipeline);
        mel_gpu_shader_destroy(p->dev, p->bg_shader);
        mel_gpu_texture_view_destroy(p->dev, p->scene_view);
        mel_gpu_texture_destroy(p->dev, p->scene);
    }
    free(p);
}

const Graphical_App POSTPROCESS_APP = {
    .title = "post-process",
    .init = post_init,
    .render = post_render,
    .teardown = post_teardown,
};
