#include "render.pipeline.forward3d.h"
#include "render.pipeline.h"
#include "render.viewport.h"
#include "render.manager.h"
#include "render.types.3d.h"
#include "render.material_base.h"
#include "render.target.h"
#include "gpu.geometry_pool.h"
#include "event.channel.h"
#include "boot.registry.h"
#include "core.engine.h"
#include "gpu.device.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.cmd.h"
#include "gpu.buffer.h"
#include "gpu.image.h"
#include "gpu.descriptor.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "math.mat4.h"
#include "async.job.h"

#include <assert.h>

typedef struct {
    Mel_Mat4 mvp;
    u32 vertex_offset;
    u32 index_offset;
    u32 material_idx;
    u32 _pad;
} Forward3D_Push;

_Static_assert(sizeof(Forward3D_Push) == 80, "Forward3D_Push must be 80 bytes (within 128 push constant limit)");

typedef struct {
    u32 strategy;
    Mel_Gpu_Image depth_image;
    u32 last_width;
    u32 last_height;
} Pipeline_Forward3D_Data;

typedef enum {
    PIPELINE_FORWARD3D_STRATEGY_NONE = 0,
    PIPELINE_FORWARD3D_STRATEGY_CLASSIC_VERTEX_PULLING,
} Pipeline_Forward3D_Strategy;

typedef struct {
    Mel_Mat4 model;
    Mel_Geometry_Handle mesh;
    u32 material_base_id;
    u32 material_idx;
    u32 flags;
    u32 layer_mask;
} Pipeline_Forward3D_Item;

typedef struct {
    u32 group;
    u32 start;
    u32 count;
} Pipeline_Forward3D_Range;

typedef struct {
    Pipeline_Forward3D_Item* items;
    Pipeline_Forward3D_Range* ranges;
    u32 item_capacity;
    u32 range_capacity;
    u32 item_count;
    u32 range_count;
    u64 synced_serial;
} Pipeline_Forward3D_Scene_Data;

static Mel_Gpu_Device* s_dev;
static Mel_Gpu_Shader s_shader;
static Mel_Gpu_Pipeline s_gpu_pipeline;
static Mel_Geometry_Pool* s_geometry_pool;
static bool s_ready;

Mel_Event_Channel mel_pipeline_forward3d_ready;

static Pipeline_Forward3D_Strategy forward3d_pick_strategy(Mel_Gpu_Device* dev)
{
    (void)dev;
    return PIPELINE_FORWARD3D_STRATEGY_CLASSIC_VERTEX_PULLING;
}

static void ensure_depth(Pipeline_Forward3D_Data* d, u32 w, u32 h)
{
    if (d->last_width == w && d->last_height == h && d->depth_image._handle != nullptr)
        return;

    if (d->depth_image._handle != nullptr)
        mel_gpu_image_shutdown(&d->depth_image, s_dev);

    mel_gpu_image_init(&d->depth_image, s_dev,
        .width = w, .height = h,
        .format = MEL_GPU_FORMAT_D32_SFLOAT,
        .usage = MEL_GPU_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT,
        .aspect = MEL_GPU_ASPECT_DEPTH,
        .mip_levels = 1, .layer_count = 1);

    d->last_width = w;
    d->last_height = h;
}

static void forward3d_scene_init(Mel_Render_Pipeline_Scene* self, Mel_Render_Manager* mgr, Mel_Gpu_Device* dev)
{
    (void)self;
    (void)mgr;
    (void)dev;
}

static void forward3d_scene_ensure_capacity(Pipeline_Forward3D_Scene_Data* data,
                                            const Mel_Alloc* alloc,
                                            u32 item_capacity,
                                            u32 range_capacity)
{
    if (item_capacity > data->item_capacity)
    {
        u32 new_capacity = data->item_capacity ? data->item_capacity : 64;
        while (new_capacity < item_capacity)
            new_capacity *= 2;

        data->items = data->items
            ? mel_realloc(alloc, data->items, (usize)new_capacity * sizeof(Pipeline_Forward3D_Item))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Pipeline_Forward3D_Item));
        data->item_capacity = new_capacity;
    }

    if (range_capacity > data->range_capacity)
    {
        u32 new_capacity = data->range_capacity ? data->range_capacity : 8;
        while (new_capacity < range_capacity)
            new_capacity *= 2;

        data->ranges = data->ranges
            ? mel_realloc(alloc, data->ranges, (usize)new_capacity * sizeof(Pipeline_Forward3D_Range))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Pipeline_Forward3D_Range));
        data->range_capacity = new_capacity;
    }
}

static void forward3d_scene_sync(Mel_Render_Pipeline_Scene* self, Mel_Render_Manager* mgr)
{
    Pipeline_Forward3D_Scene_Data* data = mel_pipeline_scene_instance(self);
    u64 serial = mel_mgr_mutation_serial(mgr);

    if (data->synced_serial != serial)
    {
        const Mel_Render_Object* objects = mel_mgr_objects(mgr);
        u32 object_count = mel_mgr_count(mgr);
        u32 base_count = mel_material_base_count();
        u32 counts[MEL_MATERIAL_BASE_MAX] = {0};

        u32 item_count = 0;
        u32 range_count = 0;
        for (u32 i = 0; i < object_count; i++)
        {
            const Mel_Render_Object* object = &objects[i];
            if (object->kind != MEL_RENDER_OBJECT_MESH_3D)
                continue;
            if (object->material_base_id >= base_count || object->material_base_id >= MEL_MATERIAL_BASE_MAX)
                continue;

            Mel_Material_Base* base = mel_material_base_get(object->material_base_id);
            if (base == nullptr || !(base->compat & MEL_COMPAT_FORWARD))
                continue;

            if (counts[object->material_base_id]++ == 0)
                range_count++;
            item_count++;
        }

        forward3d_scene_ensure_capacity(data, self->alloc, item_count, range_count);

        data->item_count = item_count;
        data->range_count = 0;

        u32 starts[MEL_MATERIAL_BASE_MAX] = {0};
        u32 cursor = 0;
        for (u32 base_id = 0; base_id < base_count && base_id < MEL_MATERIAL_BASE_MAX; base_id++)
        {
            if (counts[base_id] == 0)
                continue;

            data->ranges[data->range_count++] = (Pipeline_Forward3D_Range){
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
            if (object->kind != MEL_RENDER_OBJECT_MESH_3D)
                continue;
            if (object->material_base_id >= base_count || object->material_base_id >= MEL_MATERIAL_BASE_MAX)
                continue;
            if (counts[object->material_base_id] == 0)
                continue;

            u32 slot = fill[object->material_base_id]++;
            data->items[slot] = (Pipeline_Forward3D_Item){
                .model = object->mesh3d.model,
                .mesh = object->mesh,
                .material_base_id = object->material_base_id,
                .material_idx = object->material_idx,
                .flags = object->flags,
                .layer_mask = object->layer_mask,
            };
        }

        data->synced_serial = serial;
    }

    for (u32 i = 0; i < data->range_count; i++)
    {
        u32 base_id = data->ranges[i].group;
        Mel_Material_Base* base = mel_material_base_get(base_id);
        if (base == nullptr || base->instance_count == 0)
            continue;

        mel_material_base_upload_dirty(base_id, self->dev);
    }
}

static void forward3d_view_init(Mel_Render_Pipeline* self, Mel_Render_View* view, Mel_Render_Pipeline_Scene* scene)
{
    Pipeline_Forward3D_Data* data = mel_pipeline_instance(self);
    assert(data != nullptr);
    data->strategy = forward3d_pick_strategy(scene->dev);
    (void)self;
    (void)view;
}

static void forward3d_begin_frame(Mel_Render_Pipeline* self, Mel_Render_View* view, Mel_Render_Pipeline_Scene* scene)
{
    (void)self;
    (void)view;
    (void)scene;
}

static void forward3d_draw_classic(Mel_Render_Pipeline* self, Mel_Render_Pipeline_Scene* scene, Mel_Render_Manager* mgr, Mel_Render_Draw_Ctx* ctx)
{
    assert(self != nullptr);
    assert(ctx != nullptr && ctx->cmd != nullptr);

    if (!s_ready || s_geometry_pool == nullptr)
        return;

    if (mgr == nullptr)
        return;

    u32 count = mel_mgr_count(mgr);
    if (count == 0)
        return;

    Mel_Render_View* view = self->view;
    assert(view != nullptr);
    Pipeline_Forward3D_Scene_Data* scene_data = mel_pipeline_scene_instance(scene);
    if (scene_data->item_count == 0)
        return;

    Pipeline_Forward3D_Data* data = mel_pipeline_instance(self);
    ensure_depth(data, ctx->target_width, ctx->target_height);

    mel_gpu_cmd_transition_image(ctx->cmd, &data->depth_image, MEL_GPU_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT);

    Mel_Gpu_Color_Attachment color_att = {
        ._image_view = mel_render_target_image_view(ctx->target),
        .layout = MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .load_op = MEL_GPU_LOAD_OP_CLEAR,
        .store_op = MEL_GPU_STORE_OP_STORE,
        .clear_r = 0.06f, .clear_g = 0.07f, .clear_b = 0.10f, .clear_a = 1.0f,
    };

    Mel_Gpu_Depth_Attachment depth_att = {
        ._image_view = data->depth_image._view,
        .layout = MEL_GPU_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT,
        .load_op = MEL_GPU_LOAD_OP_CLEAR,
        .store_op = MEL_GPU_STORE_OP_STORE,
        .clear_depth = 1.0f,
    };

    mel_gpu_cmd_begin_rendering(ctx->cmd,
        .color_attachments = &color_att, .color_count = 1,
        .depth_attachment = &depth_att,
        .render_width = ctx->target_width, .render_height = ctx->target_height);

    mel_gpu_cmd_set_viewport(ctx->cmd,
        0, (f32)ctx->target_height, (f32)ctx->target_width, -(f32)ctx->target_height, 0, 1);
    mel_gpu_cmd_set_scissor(ctx->cmd, 0, 0, ctx->target_width, ctx->target_height);

    mel_gpu_cmd_bind_pipeline(ctx->cmd, &s_gpu_pipeline);

    Mel_Gpu_Buffer* vert_buf = mel_geometry_pool_vertex_buffer(s_geometry_pool);
    Mel_Gpu_Buffer* idx_buf = mel_geometry_pool_index_buffer_u32(s_geometry_pool);

    if (vert_buf == nullptr || idx_buf == nullptr)
    {
        mel_gpu_cmd_end_rendering(ctx->cmd);
        return;
    }

    Mel_Mat4 vp = mel_mat4_mul(view->camera.projection, view->camera.view);

    for (u32 range_index = 0; range_index < scene_data->range_count; range_index++)
    {
        Pipeline_Forward3D_Range range = scene_data->ranges[range_index];
        Mel_Material_Base* base = mel_material_base_get(range.group);
        if (base == nullptr || !(base->compat & MEL_COMPAT_FORWARD))
            continue;
        if (base->instance_count == 0)
            continue;

        Mel_Gpu_Buffer* mat_buf = mel_material_base_param_buffer(range.group);
        if (mat_buf == nullptr || mat_buf->_handle == nullptr)
            continue;

        void* desc = mel_gpu_pipeline_alloc_descriptor(&s_gpu_pipeline, s_dev);
        assert(desc != nullptr);

        mel_gpu_pipeline_write_buffer_binding(&s_gpu_pipeline, s_dev, desc, 0,
            vert_buf, 0, vert_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
        mel_gpu_pipeline_write_buffer_binding(&s_gpu_pipeline, s_dev, desc, 1,
            idx_buf, 0, idx_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
        mel_gpu_pipeline_write_buffer_binding(&s_gpu_pipeline, s_dev, desc, 2,
            mat_buf, 0, mat_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);

        mel_gpu_cmd_bind_descriptor_set(ctx->cmd, &s_gpu_pipeline, desc);

        for (u32 i = range.start; i < range.start + range.count; i++)
        {
            Pipeline_Forward3D_Item* item = &scene_data->items[i];
            if (item->flags & MEL_RF_HIDDEN)
                continue;

            Mel_Mat4 mvp = mel_mat4_mul(vp, item->model);
            Mel_Geometry_Region region = mel_geometry_pool_region(s_geometry_pool, item->mesh);

            Forward3D_Push pc = {
                .mvp = mvp,
                .vertex_offset = region.vertex_offset,
                .index_offset = region.index_offset,
                .material_idx = item->material_idx,
            };

            mel_gpu_cmd_push_constants(ctx->cmd, &s_gpu_pipeline,
                MEL_GPU_SHADER_STAGE_VERTEX, 0, sizeof(pc), &pc);

            mel_gpu_cmd_draw(ctx->cmd, region.index_count, 1, 0, 0);
        }
    }

    mel_gpu_cmd_end_rendering(ctx->cmd);
}

static void forward3d_draw(Mel_Render_Pipeline* self, Mel_Render_Pipeline_Scene* scene, Mel_Render_Manager* mgr, Mel_Render_Draw_Ctx* ctx)
{
    Pipeline_Forward3D_Data* data = mel_pipeline_instance(self);
    assert(data != nullptr);

    switch ((Pipeline_Forward3D_Strategy)data->strategy)
    {
        case PIPELINE_FORWARD3D_STRATEGY_CLASSIC_VERTEX_PULLING:
            forward3d_draw_classic(self, scene, mgr, ctx);
            break;

        default:
            assert(false && "forward_3d has no valid execution strategy");
            break;
    }
}

static void forward3d_view_shutdown(Mel_Render_Pipeline* self)
{
    Pipeline_Forward3D_Data* d = mel_pipeline_instance(self);
    if (d->depth_image._handle != nullptr)
        mel_gpu_image_shutdown(&d->depth_image, s_dev);
}

static void forward3d_scene_shutdown(Mel_Render_Pipeline_Scene* self)
{
    Pipeline_Forward3D_Scene_Data* data = mel_pipeline_scene_instance(self);
    mel_dealloc(self->alloc, data->items);
    mel_dealloc(self->alloc, data->ranges);
}

void mel_pipeline_forward3d_set_geometry_pool(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);
    s_geometry_pool = pool;
}

static const Mel_Render_Pipeline_Type s_forward3d_type = {
    .name = { .data = (u8*)"forward_3d", .len = 10 },
    .scene_init = forward3d_scene_init,
    .scene_sync = forward3d_scene_sync,
    .scene_shutdown = forward3d_scene_shutdown,
    .scene_size = sizeof(Pipeline_Forward3D_Scene_Data),
    .view_init = forward3d_view_init,
    .begin_frame = forward3d_begin_frame,
    .draw = forward3d_draw,
    .view_shutdown = forward3d_view_shutdown,
    .instance_size = sizeof(Pipeline_Forward3D_Data),
};

static void mel__forward3d_compile(void* data)
{
    (void)data;

    mel_gpu_shader_load(&s_shader, .path = S8("shaders/mesh_forward.slang"), .dev = s_dev);

    Mel_Gpu_Descriptor_Binding bindings[] = {
        { .binding = 0, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
        { .binding = 1, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
        { .binding = 2, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
    };

    mel_gpu_pipeline_init(&s_gpu_pipeline, s_dev,
        .shader = &s_shader,
        .color_format = MEL_GPU_FORMAT_B8G8R8A8_SRGB,
        .depth_format = MEL_GPU_FORMAT_D32_SFLOAT,
        .blend_mode = MEL_GPU_BLEND_NONE,
        .cull_mode = MEL_GPU_CULL_BACK,
        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .depth_test = true,
        .depth_write = true,
        .push_constant_size = sizeof(Forward3D_Push),
        .push_constant_stages = MEL_GPU_SHADER_STAGE_VERTEX,
        .descriptor_bindings = bindings,
        .descriptor_binding_count = 3,
        .max_descriptor_sets = 16);

    mel_pipeline_register(&s_forward3d_type);
    s_ready = true;
    mel_event_channel_fire(&mel_pipeline_forward3d_ready, nullptr);
}

static void mel__forward3d_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    s_dev = e->dev;
    mel_job_run(e->phase_counter, mel__forward3d_compile, e->phase_counter);
}

static void mel__forward3d_on_shutdown(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;

    if (s_ready)
    {
        mel_gpu_pipeline_shutdown(&s_gpu_pipeline, s_dev);
        mel_gpu_shader_shutdown(&s_shader, s_dev);
        s_ready = false;
    }
}

static void mel__forward3d_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__forward3d_on_gpu_ready, nullptr);
    mel_event_channel_on(&mel_shutdown_begin, mel__forward3d_on_shutdown, nullptr);
}

__attribute__((constructor))
static void mel__forward3d_register(void)
{
    mel_event_channel_init(&mel_pipeline_forward3d_ready, mel_alloc_heap());
    mel__boot_register_wire(mel__forward3d_wire);
}

__attribute__((destructor))
static void mel__forward3d_unregister(void)
{
    mel_event_channel_destroy(&mel_pipeline_forward3d_ready);
}
