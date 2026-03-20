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
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "math.mat4.h"

#include <assert.h>

typedef enum {
    PIPELINE_2D_STRATEGY_NONE = 0,
    PIPELINE_2D_STRATEGY_BINDLESS_SPRITES,
    PIPELINE_2D_STRATEGY_CLASSIC_SPRITES,
} Pipeline_2D_Strategy;

typedef struct {
    Pipeline_2D_Strategy strategy;
    Mel_Gpu_Image depth_image;
    u32 depth_width;
    u32 depth_height;
} Pipeline_2D_Data;

typedef struct {
    u32 group;
    u32 start;
    u32 count;
} Pipeline_2D_Range;

typedef struct {
    Mel_Render_Transform_2D* transforms;
    Mel_Render_Sprite_Info* infos;
    u32* draw_order;
    Pipeline_2D_Range* ranges;
    u32 sprite_capacity;
    u32 range_capacity;
    u32 sprite_count;
    u32 range_count;
    Mel_Gpu_Buffer transform_buffer;
    Mel_Gpu_Buffer info_buffer;
    Mel_Gpu_Buffer draw_order_buffer;
    u64 synced_serial;
} Pipeline_2D_Scene_Data;

static Mel_Gpu_Device* s_dev;

Mel_Event_Channel mel_pipeline_2d_ready;

static Pipeline_2D_Strategy pipeline_2d_pick_strategy(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Capabilities caps = mel_gpu_capabilities(dev);
    if (caps.descriptor_indexing)
        return PIPELINE_2D_STRATEGY_BINDLESS_SPRITES;

    return PIPELINE_2D_STRATEGY_CLASSIC_SPRITES;
}

static void pipeline_2d_scene_init(Mel_Render_Pipeline_Scene* self, Mel_Render_Manager* mgr, Mel_Gpu_Device* dev)
{
    (void)self;
    (void)mgr;
    (void)dev;
}

static void pipeline_2d_scene_ensure_capacity(Pipeline_2D_Scene_Data* data, const Mel_Alloc* alloc,
                                              u32 sprite_capacity, u32 range_capacity)
{
    if (sprite_capacity > data->sprite_capacity)
    {
        u32 new_capacity = data->sprite_capacity ? data->sprite_capacity : 64;
        while (new_capacity < sprite_capacity)
            new_capacity *= 2;

        data->transforms = data->transforms
            ? mel_realloc(alloc, data->transforms, (usize)new_capacity * sizeof(Mel_Render_Transform_2D))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Mel_Render_Transform_2D));
        data->infos = data->infos
            ? mel_realloc(alloc, data->infos, (usize)new_capacity * sizeof(Mel_Render_Sprite_Info))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Mel_Render_Sprite_Info));
        data->draw_order = data->draw_order
            ? mel_realloc(alloc, data->draw_order, (usize)new_capacity * sizeof(u32))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(u32));
        data->sprite_capacity = new_capacity;
    }

    if (range_capacity > data->range_capacity)
    {
        u32 new_capacity = data->range_capacity ? data->range_capacity : 8;
        while (new_capacity < range_capacity)
            new_capacity *= 2;

        data->ranges = data->ranges
            ? mel_realloc(alloc, data->ranges, (usize)new_capacity * sizeof(Pipeline_2D_Range))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Pipeline_2D_Range));
        data->range_capacity = new_capacity;
    }
}

static void pipeline_2d_scene_upload_buffer(Mel_Gpu_Buffer* buffer, Mel_Gpu_Device* dev,
                                            u64 size, const void* data)
{
    if (size == 0)
        return;

    if (buffer->_handle == nullptr || buffer->size < size)
    {
        if (buffer->_handle != nullptr)
            mel_gpu_buffer_shutdown(buffer, dev);

        mel_gpu_buffer_init(buffer, dev,
            .size = size,
            .usage = MEL_GPU_BUFFER_USAGE_STORAGE | MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
            .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU);
    }

    mel_gpu_buffer_upload(buffer, dev, data, size, 0);
}

static void pipeline_2d_scene_sync(Mel_Render_Pipeline_Scene* self, Mel_Render_Manager* mgr)
{
    Pipeline_2D_Scene_Data* data = mel_pipeline_scene_instance(self);
    u64 serial = mel_mgr_mutation_serial(mgr);

    if (data->synced_serial != serial)
    {
        const Mel_Render_Object* objects = mel_mgr_objects(mgr);
        u32 object_count = mel_mgr_count(mgr);
        u32 base_count = mel_material_base_count();
        u32 counts[MEL_MATERIAL_BASE_MAX] = {0};

        u32 sprite_count = 0;
        u32 range_count = 0;
        for (u32 i = 0; i < object_count; i++)
        {
            const Mel_Render_Object* object = &objects[i];
            if (object->kind != MEL_RENDER_OBJECT_SPRITE_2D)
                continue;
            if (object->material_base_id >= base_count || object->material_base_id >= MEL_MATERIAL_BASE_MAX)
                continue;

            Mel_Material_Base* mat = mel_material_base_get(object->material_base_id);
            if (mat == nullptr || !(mat->compat & MEL_COMPAT_2D))
                continue;

            if (counts[object->material_base_id]++ == 0)
                range_count++;
            sprite_count++;
        }

        pipeline_2d_scene_ensure_capacity(data, self->alloc, sprite_count, range_count);

        data->sprite_count = sprite_count;
        data->range_count = 0;

        u32 starts[MEL_MATERIAL_BASE_MAX] = {0};
        u32 cursor = 0;
        for (u32 base_id = 0; base_id < base_count && base_id < MEL_MATERIAL_BASE_MAX; base_id++)
        {
            if (counts[base_id] == 0)
                continue;

            data->ranges[data->range_count++] = (Pipeline_2D_Range){
                .group = base_id,
                .start = cursor,
                .count = counts[base_id],
            };
            starts[base_id] = cursor;
            cursor += counts[base_id];
        }

        u32 fill[MEL_MATERIAL_BASE_MAX] = {0};
        for (u32 base_id = 0; base_id < base_count && base_id < MEL_MATERIAL_BASE_MAX; base_id++)
            fill[base_id] = starts[base_id];

        for (u32 i = 0; i < object_count; i++)
        {
            const Mel_Render_Object* object = &objects[i];
            if (object->kind != MEL_RENDER_OBJECT_SPRITE_2D)
                continue;
            if (object->material_base_id >= base_count || object->material_base_id >= MEL_MATERIAL_BASE_MAX)
                continue;
            if (counts[object->material_base_id] == 0)
                continue;

            u32 slot = fill[object->material_base_id]++;
            data->transforms[slot] = (Mel_Render_Transform_2D){
                .pos = object->sprite2d.pos,
                .scale = object->sprite2d.scale,
                .rotation = object->sprite2d.rotation,
                .depth = object->sprite2d.depth,
                .flags = object->sprite2d.flags,
            };
            data->infos[slot] = (Mel_Render_Sprite_Info){
                .uv = object->uv,
                .color = object->color,
                .texture_idx = object->texture_idx,
                .material_base_id = object->material_base_id,
                .layer = object->material_idx,
            };
            data->draw_order[slot] = slot;
        }

        if (sprite_count > 0)
        {
            pipeline_2d_scene_upload_buffer(&data->transform_buffer, self->dev,
                (u64)sprite_count * sizeof(Mel_Render_Transform_2D), data->transforms);
            pipeline_2d_scene_upload_buffer(&data->info_buffer, self->dev,
                (u64)sprite_count * sizeof(Mel_Render_Sprite_Info), data->infos);
            pipeline_2d_scene_upload_buffer(&data->draw_order_buffer, self->dev,
                (u64)sprite_count * sizeof(u32), data->draw_order);
        }

        data->synced_serial = serial;
    }

    for (u32 i = 0; i < data->range_count; i++)
        mel_material_base_upload_dirty(data->ranges[i].group, self->dev);
}

static void pipeline_2d_view_init(Mel_Render_Pipeline* self, Mel_Render_View* view, Mel_Render_Pipeline_Scene* scene)
{
    (void)view;
    Pipeline_2D_Data* data = mel_pipeline_instance(self);
    assert(data != nullptr);

    data->strategy = pipeline_2d_pick_strategy(scene->dev);
}

static void pipeline_2d_begin_frame(Mel_Render_Pipeline* self, Mel_Render_View* view, Mel_Render_Pipeline_Scene* scene)
{
    (void)self;
    (void)view;
    (void)scene;
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

static void draw_strategy_bindless(Mel_Render_Pipeline* self, Mel_Render_Pipeline_Scene* scene, Mel_Render_Draw_Ctx* ctx)
{
    Mel_Texture_Table* texture_table = mel_texture_pool_get_table();
    assert(texture_table != nullptr);

    Mel_Render_View* view = self->view;
    Pipeline_2D_Data* data = mel_pipeline_instance(self);
    Pipeline_2D_Scene_Data* scene_data = mel_pipeline_scene_instance(scene);

    mel__pipeline_2d_ensure_depth(data, ctx->target_width, ctx->target_height);

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

    Mel_Gpu_Buffer* transform_buf = &scene_data->transform_buffer;
    Mel_Gpu_Buffer* sprite_buf = &scene_data->info_buffer;
    Mel_Gpu_Buffer* draw_order_buf = &scene_data->draw_order_buffer;

    for (u32 i = 0; i < scene_data->range_count; i++)
    {
        Pipeline_2D_Range range = scene_data->ranges[i];
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

static void draw_strategy_classic(Mel_Render_Pipeline* self, Mel_Render_Manager* mgr, Mel_Render_Draw_Ctx* ctx)
{
    (void)self;
    (void)mgr;
    (void)ctx;
    assert(false && "Classic sprite execution requires a dedicated non-bindless shader variant that does not yet exist");
}

static void pipeline_2d_draw(Mel_Render_Pipeline* self, Mel_Render_Pipeline_Scene* scene, Mel_Render_Manager* mgr, Mel_Render_Draw_Ctx* ctx)
{
    assert(self != nullptr);
    assert(ctx != nullptr);
    assert(ctx->cmd != nullptr);
    (void)scene;

    if (mel_texture_pool_get_table() == nullptr || mel_texture_pool_get_table()->capacity == 0)
        return;

    assert(mgr != nullptr);

    Pipeline_2D_Scene_Data* scene_data = mel_pipeline_scene_instance(scene);
    u32 sprite_count = scene_data->sprite_count;
    if (sprite_count == 0)
        return;

    assert(self->view != nullptr);

    Pipeline_2D_Data* data = mel_pipeline_instance(self);
    assert(data != nullptr);

    switch (data->strategy)
    {
        case PIPELINE_2D_STRATEGY_BINDLESS_SPRITES:
            draw_strategy_bindless(self, scene, ctx);
            break;

        case PIPELINE_2D_STRATEGY_CLASSIC_SPRITES:
            draw_strategy_classic(self, mgr, ctx);
            break;

        default:
            assert(false && "default_2d has no valid execution strategy");
            break;
    }
}

static void pipeline_2d_scene_shutdown(Mel_Render_Pipeline_Scene* self)
{
    Pipeline_2D_Scene_Data* data = mel_pipeline_scene_instance(self);

    if (data->transform_buffer._handle != nullptr)
        mel_gpu_buffer_shutdown(&data->transform_buffer, self->dev);
    if (data->info_buffer._handle != nullptr)
        mel_gpu_buffer_shutdown(&data->info_buffer, self->dev);
    if (data->draw_order_buffer._handle != nullptr)
        mel_gpu_buffer_shutdown(&data->draw_order_buffer, self->dev);

    mel_dealloc(self->alloc, data->transforms);
    mel_dealloc(self->alloc, data->infos);
    mel_dealloc(self->alloc, data->draw_order);
    mel_dealloc(self->alloc, data->ranges);
}

static void pipeline_2d_shutdown(Mel_Render_Pipeline* self)
{
    Pipeline_2D_Data* d = mel_pipeline_instance(self);
    if (d->depth_image._handle != nullptr)
        mel_gpu_image_shutdown(&d->depth_image, s_dev);
}


static const Mel_Render_Pipeline_Type s_pipeline_2d_type = {
    .name = { .data = (u8*)"default_2d", .len = 10 },
    .scene_init = pipeline_2d_scene_init,
    .scene_sync = pipeline_2d_scene_sync,
    .scene_shutdown = pipeline_2d_scene_shutdown,
    .scene_size = sizeof(Pipeline_2D_Scene_Data),
    .view_init = pipeline_2d_view_init,
    .begin_frame = pipeline_2d_begin_frame,
    .draw = pipeline_2d_draw,
    .view_shutdown = pipeline_2d_shutdown,
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
