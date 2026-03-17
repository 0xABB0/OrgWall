#include "render.pipeline.2d.h"
#include "render.pipeline.h"
#include "render.viewport.h"
#include "render.manager.2d.h"
#include "render.sprite2d.shader.h"
#include "render.texture_table.h"
#include "render.target.h"
#include "event.channel.h"
#include "boot.registry.h"
#include "gpu.device.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.cmd.h"
#include "gpu.buffer.h"
#include "gpu.descriptor.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "math.mat4.h"
#include "async.job.h"

#include <assert.h>

#define PIPELINE_2D_TIER_BINDLESS    2
#define PIPELINE_2D_TIER_TRADITIONAL 4

typedef struct {
    u32 tier;
} Pipeline_2D_Data;

static Mel_Gpu_Device*   s_dev;
static Mel_Gpu_Shader    s_shader;
static Mel_Gpu_Pipeline  s_gpu_pipeline;
static Mel_Texture_Table* s_texture_table;
static bool              s_ready;

Mel_Event_Channel mel_pipeline_2d_ready;

static void pipeline_2d_init(Mel_Render_Pipeline* self, Mel_Render_View* view)
{
    (void)view;
    Pipeline_2D_Data* data = mel_pipeline_instance(self);
    assert(data != nullptr);

    Mel_Gpu_Capabilities caps = mel_gpu_capabilities(s_dev);
    data->tier = caps.descriptor_indexing ? PIPELINE_2D_TIER_BINDLESS : PIPELINE_2D_TIER_TRADITIONAL;
}

static void draw_tier2(Mel_Render_Pipeline* self, Mel_Render_Manager_2D* mgr2d, Mel_Render_Draw_Ctx* ctx, u32 sprite_count)
{
    assert(s_texture_table != nullptr);

    Mel_Render_View* view = self->view;

    Mel_Gpu_Color_Attachment color_att = {
        ._image_view = mel_render_target_image_view(ctx->target),
        .layout      = MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .load_op     = MEL_GPU_LOAD_OP_CLEAR,
        .store_op    = MEL_GPU_STORE_OP_STORE,
        .clear_r     = 0.0f,
        .clear_g     = 0.0f,
        .clear_b     = 0.0f,
        .clear_a     = 1.0f,
    };

    mel_gpu_cmd_begin_rendering(ctx->cmd,
        .color_attachments = &color_att,
        .color_count       = 1,
        .render_width      = ctx->target_width,
        .render_height     = ctx->target_height);

    mel_gpu_cmd_set_viewport(ctx->cmd,
        0.0f,
        (f32)ctx->target_height,
        (f32)ctx->target_width,
        -(f32)ctx->target_height,
        0.0f,
        1.0f);

    mel_gpu_cmd_set_scissor(ctx->cmd, 0, 0, ctx->target_width, ctx->target_height);

    mel_gpu_cmd_bind_pipeline(ctx->cmd, &s_gpu_pipeline);

    void* desc = mel_gpu_pipeline_alloc_descriptor(&s_gpu_pipeline, s_dev);
    assert(desc != nullptr);

    Mel_Gpu_Buffer* transform_buf  = mel_mgr_2d_transform_buffer(mgr2d);
    Mel_Gpu_Buffer* sprite_buf     = mel_mgr_2d_sprite_info_buffer(mgr2d);

    mel_gpu_pipeline_write_buffer_binding(&s_gpu_pipeline, s_dev, desc, 0,
        transform_buf, 0, transform_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_buffer_binding(&s_gpu_pipeline, s_dev, desc, 1,
        sprite_buf, 0, sprite_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);

    mel_gpu_cmd_bind_descriptor_set(ctx->cmd, &s_gpu_pipeline, desc);

    mel_texture_table_bind(s_texture_table, ctx->cmd, s_gpu_pipeline._layout, 1);

    Mel_Sprite2D_Push_Constants pc = { .projection = view->camera.projection };
    mel_gpu_cmd_push_constants(ctx->cmd, &s_gpu_pipeline,
        MEL_GPU_SHADER_STAGE_VERTEX, 0, sizeof(pc), &pc);

    mel_gpu_cmd_draw(ctx->cmd, 6, sprite_count, 0, 0);

    mel_gpu_cmd_end_rendering(ctx->cmd);
}

static void draw_tier4(Mel_Render_Pipeline* self, Mel_Render_Manager_2D* mgr2d, Mel_Render_Draw_Ctx* ctx, u32 sprite_count)
{
    (void)self;
    (void)mgr2d;
    (void)ctx;
    (void)sprite_count;
    assert(false && "Tier 4 (traditional VB/IB) requires a dedicated non-bindless shader variant that does not yet exist");
}

static void pipeline_2d_draw(Mel_Render_Pipeline* self, void* mgr, Mel_Render_Draw_Ctx* ctx)
{
    assert(self != nullptr);
    assert(ctx != nullptr);
    assert(ctx->cmd != nullptr);

    if (!s_ready)
        return;

    Mel_Render_Manager_2D* mgr2d = mgr;
    assert(mgr2d != nullptr);

    mel_mgr_2d_upload_dirty(mgr2d);

    u32 sprite_count = mel_mgr_2d_count(mgr2d);
    if (sprite_count == 0)
        return;

    assert(self->view != nullptr);

    Pipeline_2D_Data* data = mel_pipeline_instance(self);
    assert(data != nullptr);

    if (data->tier == PIPELINE_2D_TIER_BINDLESS)
        draw_tier2(self, mgr2d, ctx, sprite_count);
    else
        draw_tier4(self, mgr2d, ctx, sprite_count);
}

static void pipeline_2d_shutdown(Mel_Render_Pipeline* self)
{
    (void)self;
}

static void mel__pipeline_2d_create_gpu_pipeline(void)
{
    assert(s_texture_table != nullptr);

    Mel_Gpu_Descriptor_Binding bindings[] = {
        {
            .binding = 0,
            .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER,
            .count = 1,
            .stages = MEL_GPU_SHADER_STAGE_VERTEX,
        },
        {
            .binding = 1,
            .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER,
            .count = 1,
            .stages = MEL_GPU_SHADER_STAGE_VERTEX,
        },
    };

    void* extra_layouts[] = { s_texture_table->layout._layout };

    mel_gpu_pipeline_init(&s_gpu_pipeline, s_dev,
        .shader = &s_shader,
        .color_format = MEL_GPU_FORMAT_B8G8R8A8_SRGB,
        .blend_mode = MEL_GPU_BLEND_ALPHA,
        .cull_mode = MEL_GPU_CULL_NONE,
        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .depth_test = false,
        .depth_write = false,
        .push_constant_size = sizeof(Mel_Sprite2D_Push_Constants),
        .push_constant_stages = MEL_GPU_SHADER_STAGE_VERTEX,
        .descriptor_bindings = bindings,
        .descriptor_binding_count = 2,
        .extra_set_layouts = extra_layouts,
        .extra_set_layout_count = 1,
        .max_descriptor_sets = 16);

    s_ready = true;
}

void mel_pipeline_2d_set_texture_table(Mel_Texture_Table* tt)
{
    assert(tt != nullptr);
    s_texture_table = tt;

    if (s_shader._vertex != nullptr && s_gpu_pipeline._pipeline == nullptr)
        mel__pipeline_2d_create_gpu_pipeline();
}

static const Mel_Render_Pipeline_Type s_pipeline_2d_type = {
    .name = { .data = (u8*)"default_2d", .len = 10 },
    .init = pipeline_2d_init,
    .draw = pipeline_2d_draw,
    .shutdown = pipeline_2d_shutdown,
    .instance_size = sizeof(Pipeline_2D_Data),
};

static void mel__pipeline_2d_compile(void* data)
{
    (void)data;

    mel_gpu_shader_load(&s_shader, .path = S8("shaders/sprite_2d.slang"), .dev = s_dev);

    mel_pipeline_register(&s_pipeline_2d_type);

    if (s_texture_table != nullptr && s_gpu_pipeline._pipeline == nullptr)
        mel__pipeline_2d_create_gpu_pipeline();

    mel_event_channel_fire(&mel_pipeline_2d_ready, nullptr);
}

static void mel__pipeline_2d_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    s_dev = e->dev;
    mel_job_run(e->phase_counter, mel__pipeline_2d_compile, e->phase_counter);
}

static void mel__pipeline_2d_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__pipeline_2d_on_gpu_ready, nullptr);
}

__attribute__((constructor))
static void mel__pipeline_2d_register(void)
{
    mel_event_channel_init(&mel_pipeline_2d_ready, mel_alloc_heap());
    mel__boot_register_wire(mel__pipeline_2d_wire);
}

__attribute__((destructor))
static void mel__pipeline_2d_unregister(void)
{
    mel_event_channel_destroy(&mel_pipeline_2d_ready);
}
