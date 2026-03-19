#include "render.pipeline.2d.h"
#include "render.pipeline.h"
#include "render.viewport.h"
#include "render.manager.h"
#include "render.types.2d.h"
#include "render.sprite2d.shader.h"
#include "render.texture_table.h"
#include "render.material_base.h"
#include "render.target.h"
#include "texture.pool.h"
#include "event.channel.h"
#include "boot.registry.h"
#include "core.engine.h"
#include "gpu.device.h"
#include "gpu.pipeline.h"
#include "gpu.pipeline_cache.h"
#include "gpu.cmd.h"
#include "gpu.buffer.h"
#include "gpu.image.h"
#include "gpu.descriptor.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "math.mat4.h"

#include <assert.h>

#define PIPELINE_2D_TIER_BINDLESS    2
#define PIPELINE_2D_TIER_TRADITIONAL 4

typedef struct {
    u32 tier;
    Mel_Gpu_Image depth_image;
    u32 depth_width;
    u32 depth_height;
} Pipeline_2D_Data;

static Mel_Gpu_Device* s_dev;

Mel_Event_Channel mel_pipeline_2d_ready;

static void pipeline_2d_init(Mel_Render_Pipeline* self, Mel_Render_View* view)
{
    (void)view;
    Pipeline_2D_Data* data = mel_pipeline_instance(self);
    assert(data != nullptr);

    Mel_Gpu_Capabilities caps = mel_gpu_capabilities(s_dev);
    data->tier = caps.descriptor_indexing ? PIPELINE_2D_TIER_BINDLESS : PIPELINE_2D_TIER_TRADITIONAL;
}

static Mel_Gpu_Pipeline* mel__pipeline_2d_get_cached(
    Mel_Material_Base* mat, Mel_Gpu_Format target_format)
{
    assert(mat != nullptr);
    assert(mat->shader != nullptr);
    Mel_Texture_Table* texture_table = mel_texture_pool_get_table();
    assert(texture_table != nullptr);

    u32 binding_count;
    Mel_Gpu_Descriptor_Binding bindings[4];

    bindings[0] = (Mel_Gpu_Descriptor_Binding){
        .binding = 0, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER,
        .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX };
    bindings[1] = (Mel_Gpu_Descriptor_Binding){
        .binding = 1, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER,
        .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX };

    if (mat->param_size > 0)
    {
        bindings[2] = (Mel_Gpu_Descriptor_Binding){
            .binding = 2, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER,
            .count = 1, .stages = MEL_GPU_SHADER_STAGE_FRAGMENT };
        bindings[3] = (Mel_Gpu_Descriptor_Binding){
            .binding = 3, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER,
            .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX };
        binding_count = 4;
    }
    else
    {
        bindings[2] = (Mel_Gpu_Descriptor_Binding){
            .binding = 2, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER,
            .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX };
        binding_count = 3;
    }

    void* extra_layouts[] = { texture_table->layout._layout };

    return mel_gpu_pipeline_cache_get(s_dev->pipeline_cache, s_dev,
        .shader = mat->shader,
        .color_format = target_format,
        .blend_mode = MEL_GPU_BLEND_ALPHA,
        .cull_mode = MEL_GPU_CULL_NONE,
        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .depth_test = true,
        .depth_write = true,
        .depth_format = MEL_GPU_FORMAT_D32_SFLOAT,
        .push_constant_size = sizeof(Mel_Sprite2D_Push_Constants),
        .push_constant_stages = MEL_GPU_SHADER_STAGE_VERTEX,
        .descriptor_bindings = bindings,
        .descriptor_binding_count = binding_count,
        .extra_set_layouts = extra_layouts,
        .extra_set_layout_count = 1,
        .max_descriptor_sets = 16);
}

static void mel__pipeline_2d_ensure_depth(Pipeline_2D_Data* d, u32 w, u32 h)
{
    if (d->depth_width == w && d->depth_height == h && d->depth_image._handle != nullptr)
        return;

    if (d->depth_image._handle != nullptr)
        mel_gpu_image_shutdown(&d->depth_image, s_dev);

    mel_gpu_image_init(&d->depth_image, s_dev,
        .width = w, .height = h,
        .format = MEL_GPU_FORMAT_D32_SFLOAT,
        .usage = MEL_GPU_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT,
        .aspect = MEL_GPU_ASPECT_DEPTH,
        .alloc = mel_alloc_heap());

    d->depth_width = w;
    d->depth_height = h;
}

static void draw_tier2(Mel_Render_Pipeline* self, Mel_Render_Manager* mgr, Mel_Render_Draw_Ctx* ctx)
{
    Mel_Texture_Table* texture_table = mel_texture_pool_get_table();
    assert(texture_table != nullptr);

    Mel_Render_View* view = self->view;
    Pipeline_2D_Data* data = mel_pipeline_instance(self);

    mel__pipeline_2d_ensure_depth(data, ctx->target_width, ctx->target_height);

    u32 range_count = mel_mgr_group_count(mgr);
    const Mel_Mgr_Range* ranges = mel_mgr_group_ranges(mgr);

    for (u32 i = 0; i < range_count; i++)
        mel_material_base_upload_dirty(ranges[i].group, s_dev);

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

    Mel_Gpu_Depth_Attachment depth_att = {
        ._image_view = data->depth_image._view,
        .layout      = MEL_GPU_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT,
        .load_op     = MEL_GPU_LOAD_OP_CLEAR,
        .store_op    = MEL_GPU_STORE_OP_DONT_CARE,
        .clear_depth = 1.0f,
    };

    mel_gpu_cmd_begin_rendering(ctx->cmd,
        .color_attachments = &color_att,
        .color_count       = 1,
        .depth_attachment  = &depth_att,
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

    Mel_Gpu_Buffer* transform_buf   = mel_mgr_pool_buffer(mgr, MEL_2D_POOL_TRANSFORMS);
    Mel_Gpu_Buffer* sprite_buf      = mel_mgr_pool_buffer(mgr, MEL_2D_POOL_INFOS);
    Mel_Gpu_Buffer* draw_order_buf  = mel_mgr_draw_order_buffer(mgr);

    for (u32 i = 0; i < range_count; i++)
    {
        Mel_Mgr_Range range = ranges[i];
        Mel_Material_Base* mat = mel_material_base_get(range.group);

        if (!mat->shader_ready)
            continue;

        Mel_Gpu_Pipeline* gpu_pipe = mel__pipeline_2d_get_cached(mat, ctx->target_format);

        mel_gpu_cmd_bind_pipeline(ctx->cmd, gpu_pipe);

        void* desc = mel_gpu_pipeline_alloc_descriptor(gpu_pipe, s_dev);
        assert(desc != nullptr);

        mel_gpu_pipeline_write_buffer_binding(gpu_pipe, s_dev, desc, 0,
            transform_buf, 0, transform_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
        mel_gpu_pipeline_write_buffer_binding(gpu_pipe, s_dev, desc, 1,
            sprite_buf, 0, sprite_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);

        if (mat->param_size > 0)
        {
            Mel_Gpu_Buffer* param_buf = mel_material_base_param_buffer(range.group);
            mel_gpu_pipeline_write_buffer_binding(gpu_pipe, s_dev, desc, 2,
                param_buf, 0, param_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
            mel_gpu_pipeline_write_buffer_binding(gpu_pipe, s_dev, desc, 3,
                draw_order_buf, 0, draw_order_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
        }
        else
        {
            mel_gpu_pipeline_write_buffer_binding(gpu_pipe, s_dev, desc, 2,
                draw_order_buf, 0, draw_order_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
        }

        mel_gpu_cmd_bind_descriptor_set(ctx->cmd, gpu_pipe, desc);

        mel_texture_table_bind(texture_table, ctx->cmd, gpu_pipe->_layout, 1);

        Mel_Sprite2D_Push_Constants pc = {
            .projection = view->camera.projection,
            .draw_offset = range.start,
        };
        mel_gpu_cmd_push_constants(ctx->cmd, gpu_pipe,
            MEL_GPU_SHADER_STAGE_VERTEX, 0, sizeof(pc), &pc);

        mel_gpu_cmd_draw(ctx->cmd, 6, range.count, 0, 0);
    }

    mel_gpu_cmd_end_rendering(ctx->cmd);
}

static void draw_tier4(Mel_Render_Pipeline* self, Mel_Render_Manager* mgr, Mel_Render_Draw_Ctx* ctx)
{
    (void)self;
    (void)mgr;
    (void)ctx;
    assert(false && "Tier 4 (traditional VB/IB) requires a dedicated non-bindless shader variant that does not yet exist");
}

static void pipeline_2d_draw(Mel_Render_Pipeline* self, void* mgr, Mel_Render_Draw_Ctx* ctx)
{
    assert(self != nullptr);
    assert(ctx != nullptr);
    assert(ctx->cmd != nullptr);

    if (mel_texture_pool_get_table() == nullptr || mel_texture_pool_get_table()->capacity == 0)
        return;

    Mel_Render_Manager* m = mgr;
    assert(m != nullptr);

    mel_mgr_upload_dirty(m);

    u32 sprite_count = mel_mgr_count(m);
    if (sprite_count == 0)
        return;

    assert(self->view != nullptr);

    Pipeline_2D_Data* data = mel_pipeline_instance(self);
    assert(data != nullptr);

    if (data->tier == PIPELINE_2D_TIER_BINDLESS)
        draw_tier2(self, m, ctx);
    else
        draw_tier4(self, m, ctx);
}

static void pipeline_2d_shutdown(Mel_Render_Pipeline* self)
{
    Pipeline_2D_Data* d = mel_pipeline_instance(self);
    if (d->depth_image._handle != nullptr)
        mel_gpu_image_shutdown(&d->depth_image, s_dev);
}


static const Mel_Render_Pipeline_Type s_pipeline_2d_type = {
    .name = { .data = (u8*)"default_2d", .len = 10 },
    .init = pipeline_2d_init,
    .draw = pipeline_2d_draw,
    .shutdown = pipeline_2d_shutdown,
    .instance_size = sizeof(Pipeline_2D_Data),
};

static void mel__pipeline_2d_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    s_dev = e->dev;
    mel_pipeline_register(&s_pipeline_2d_type);
    mel_event_channel_fire(&mel_pipeline_2d_ready, nullptr);
}

static void mel__pipeline_2d_on_shutdown(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;
}

static void mel__pipeline_2d_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__pipeline_2d_on_gpu_ready, nullptr);
    mel_event_channel_on(&mel_shutdown_begin, mel__pipeline_2d_on_shutdown, nullptr);
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
