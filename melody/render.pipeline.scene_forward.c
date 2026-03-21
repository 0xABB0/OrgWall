#include "render.pipeline.scene_forward.h"
#include "render.pipeline.h"
#include "render.viewport.h"
#include "render.manager.h"
#include "render.source.type.h"
#include "render.types.2d.h"
#include "render.types.3d.h"
#include "render.sprite2d.shader.h"
#include "render.texture_table.h"
#include "render.material_base.h"
#include "render.target.h"
#include "texture.pool.h"
#include "gpu.geometry_pool.h"
#include "event.channel.h"
#include "boot.registry.h"
#include "core.engine.h"
#include "gpu.device.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.pipeline_cache.h"
#include "gpu.cmd.h"
#include "gpu.buffer.h"
#include "gpu.image.h"
#include "gpu.descriptor.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "math.mat3.h"
#include "math.mat4.h"
#include "async.job.h"

#include <assert.h>

typedef struct {
    Mel_Vec4 model_row0;
    Mel_Vec4 model_row1;
    Mel_Vec4 model_row2;
    Mel_Vec4 normal_row0;
    Mel_Vec4 normal_row1;
    Mel_Vec4 normal_row2;
    u32 vertex_offset;
    u32 index_offset;
    u32 material_idx;
    u32 _pad;
} Forward3D_Push;

typedef struct {
    Mel_Mat4 view_projection;
    Mel_Vec4 camera_position;
    Mel_Vec4 light_direction;
} Scene_Forward_Mesh_View_Params;

_Static_assert(sizeof(Forward3D_Push) == 112, "Forward3D_Push must be 112 bytes");
_Static_assert(sizeof(Scene_Forward_Mesh_View_Params) == 96, "Scene_Forward_Mesh_View_Params must be 96 bytes");

#define SCENE_FORWARD_MESH_STRATEGY_NONE                   0u
#define SCENE_FORWARD_MESH_STRATEGY_CLASSIC_VERTEX_PULLING 1u

#define SCENE_FORWARD_SPRITE_STRATEGY_NONE             0u
#define SCENE_FORWARD_SPRITE_STRATEGY_BINDLESS_SPRITES 1u
#define SCENE_FORWARD_SPRITE_STRATEGY_CLASSIC_SPRITES  2u

typedef struct {
    u32 mesh_strategy;
    u32 sprite_strategy;
    Mel_Gpu_Image depth_image;
    Mel_Gpu_Buffer mesh_view_buffer;
    u32 last_width;
    u32 last_height;
} Scene_Forward_View_Data;

typedef struct {
    Mel_Mat4 model;
    Mel_Geometry_Handle mesh;
    u32 material_base_id;
    u32 material_idx;
    u32 flags;
    u32 layer_mask;
} Scene_Forward_Mesh_Item;

typedef struct {
    u32 group;
    u32 start;
    u32 count;
} Scene_Forward_Mesh_Range;

typedef struct {
    u32 group;
    u32 start;
    u32 count;
} Scene_Forward_Sprite_Range;

typedef struct {
    Scene_Forward_Mesh_Item* mesh_items;
    Scene_Forward_Mesh_Range* mesh_ranges;
    u32 mesh_item_capacity;
    u32 mesh_range_capacity;
    u32 mesh_item_count;
    u32 mesh_range_count;

    Mel_Render_Transform_2D* sprite_transforms;
    Mel_Render_Sprite_Info* sprite_infos;
    u32* sprite_draw_order;
    Scene_Forward_Sprite_Range* sprite_ranges;
    u32 sprite_capacity;
    u32 sprite_range_capacity;
    u32 sprite_count;
    u32 sprite_range_count;
    Mel_Gpu_Buffer sprite_transform_buffer;
    Mel_Gpu_Buffer sprite_info_buffer;
    Mel_Gpu_Buffer sprite_draw_order_buffer;

    u64 synced_serial;
} Scene_Forward_Scene_Data;

#define SCENE_FORWARD_EMIT_MODE_COUNT 1u
#define SCENE_FORWARD_EMIT_MODE_FILL  2u

struct Mel_Scene_Forward_Emitter {
    Scene_Forward_Scene_Data* data;
    const Mel_Render_Instance* instance;
    const Mel_Render_Material_Binding* material_bindings;
    u32 material_binding_count;
    u32 mode;
    u32 base_count;
    u32* sprite_counts;
    u32* mesh_counts;
    u32* sprite_fill;
    u32* mesh_fill;
};

static const Mel_Render_Material_Binding* scene_forward_emitter_binding(
    Mel_Scene_Forward_Emitter* emitter, u32 binding_index, u32 compat_mask)
{
    assert(emitter != nullptr);

    if (binding_index >= emitter->material_binding_count)
        return nullptr;

    const Mel_Render_Material_Binding* binding = &emitter->material_bindings[binding_index];
    if (binding->material_base_id >= emitter->base_count || binding->material_base_id >= MEL_MATERIAL_BASE_MAX)
        return nullptr;

    Mel_Material_Base* base = mel_material_base_get(binding->material_base_id);
    if (base == nullptr || !(base->compat & compat_mask))
        return nullptr;

    return binding;
}

static Mel_Gpu_Device* s_dev;
static Mel_Gpu_Shader s_mesh_shader;
static Mel_Gpu_Pipeline s_mesh_pipeline;
static Mel_Geometry_Pool* s_geometry_pool;
static bool s_mesh_ready;
static bool s_scene_forward_registered;

Mel_Event_Channel mel_pipeline_scene_forward_ready;

static u32 scene_forward_pick_mesh_strategy(Mel_Gpu_Device* dev)
{
    (void)dev;
    return SCENE_FORWARD_MESH_STRATEGY_CLASSIC_VERTEX_PULLING;
}

static u32 scene_forward_pick_sprite_strategy(Mel_Gpu_Device* dev)
{
    Mel_Gpu_Capabilities caps = mel_gpu_capabilities(dev);
    if (caps.descriptor_indexing)
        return SCENE_FORWARD_SPRITE_STRATEGY_BINDLESS_SPRITES;

    return SCENE_FORWARD_SPRITE_STRATEGY_CLASSIC_SPRITES;
}

static void ensure_depth(Scene_Forward_View_Data* d, u32 w, u32 h)
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

static void scene_forward_scene_upload_buffer(Mel_Gpu_Buffer* buffer, Mel_Gpu_Device* dev,
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

static void scene_forward_scene_ensure_mesh_capacity(Scene_Forward_Scene_Data* data,
                                                     const Mel_Alloc* alloc,
                                                     u32 item_capacity,
                                                     u32 range_capacity)
{
    if (item_capacity > data->mesh_item_capacity)
    {
        u32 new_capacity = data->mesh_item_capacity ? data->mesh_item_capacity : 64;
        while (new_capacity < item_capacity)
            new_capacity *= 2;

        data->mesh_items = data->mesh_items
            ? mel_realloc(alloc, data->mesh_items, (usize)new_capacity * sizeof(Scene_Forward_Mesh_Item))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Scene_Forward_Mesh_Item));
        data->mesh_item_capacity = new_capacity;
    }

    if (range_capacity > data->mesh_range_capacity)
    {
        u32 new_capacity = data->mesh_range_capacity ? data->mesh_range_capacity : 8;
        while (new_capacity < range_capacity)
            new_capacity *= 2;

        data->mesh_ranges = data->mesh_ranges
            ? mel_realloc(alloc, data->mesh_ranges, (usize)new_capacity * sizeof(Scene_Forward_Mesh_Range))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Scene_Forward_Mesh_Range));
        data->mesh_range_capacity = new_capacity;
    }
}

static void scene_forward_scene_ensure_sprite_capacity(Scene_Forward_Scene_Data* data,
                                                       const Mel_Alloc* alloc,
                                                       u32 sprite_capacity,
                                                       u32 range_capacity)
{
    if (sprite_capacity > data->sprite_capacity)
    {
        u32 new_capacity = data->sprite_capacity ? data->sprite_capacity : 64;
        while (new_capacity < sprite_capacity)
            new_capacity *= 2;

        data->sprite_transforms = data->sprite_transforms
            ? mel_realloc(alloc, data->sprite_transforms, (usize)new_capacity * sizeof(Mel_Render_Transform_2D))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Mel_Render_Transform_2D));
        data->sprite_infos = data->sprite_infos
            ? mel_realloc(alloc, data->sprite_infos, (usize)new_capacity * sizeof(Mel_Render_Sprite_Info))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Mel_Render_Sprite_Info));
        data->sprite_draw_order = data->sprite_draw_order
            ? mel_realloc(alloc, data->sprite_draw_order, (usize)new_capacity * sizeof(u32))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(u32));
        data->sprite_capacity = new_capacity;
    }

    if (range_capacity > data->sprite_range_capacity)
    {
        u32 new_capacity = data->sprite_range_capacity ? data->sprite_range_capacity : 8;
        while (new_capacity < range_capacity)
            new_capacity *= 2;

        data->sprite_ranges = data->sprite_ranges
            ? mel_realloc(alloc, data->sprite_ranges, (usize)new_capacity * sizeof(Scene_Forward_Sprite_Range))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(Scene_Forward_Sprite_Range));
        data->sprite_range_capacity = new_capacity;
    }
}

void mel_scene_forward_emit_sprite(Mel_Scene_Forward_Emitter* emitter,
                                   const Mel_Scene_Forward_Sprite* sprite)
{
    assert(emitter != nullptr);
    assert(sprite != nullptr);
    assert(emitter->instance != nullptr);

    const Mel_Render_Material_Binding* binding =
        scene_forward_emitter_binding(emitter, sprite->material_binding_index, MEL_COMPAT_2D);
    if (binding == nullptr)
        return;
    u32 base_id = binding->material_base_id;

    if (emitter->mode == SCENE_FORWARD_EMIT_MODE_COUNT)
    {
        emitter->sprite_counts[base_id]++;
        return;
    }

    if (emitter->mode != SCENE_FORWARD_EMIT_MODE_FILL)
        return;

    u32 slot = emitter->sprite_fill[base_id]++;
    emitter->data->sprite_transforms[slot] = sprite->transform;
    emitter->data->sprite_infos[slot] = (Mel_Render_Sprite_Info){
        .uv = sprite->uv,
        .color = sprite->color,
        .texture_idx = sprite->texture_idx,
        .material_base_id = binding->material_base_id,
        .layer = binding->material_idx,
    };
    emitter->data->sprite_draw_order[slot] = slot;
}

void mel_scene_forward_emit_mesh(Mel_Scene_Forward_Emitter* emitter,
                                 const Mel_Scene_Forward_Mesh* mesh)
{
    assert(emitter != nullptr);
    assert(mesh != nullptr);
    assert(emitter->instance != nullptr);

    const Mel_Render_Material_Binding* binding =
        scene_forward_emitter_binding(emitter, mesh->material_binding_index, MEL_COMPAT_FORWARD);
    if (binding == nullptr)
        return;
    u32 base_id = binding->material_base_id;

    if (emitter->mode == SCENE_FORWARD_EMIT_MODE_COUNT)
    {
        emitter->mesh_counts[base_id]++;
        return;
    }

    if (emitter->mode != SCENE_FORWARD_EMIT_MODE_FILL)
        return;

    u32 slot = emitter->mesh_fill[base_id]++;
    emitter->data->mesh_items[slot] = (Scene_Forward_Mesh_Item){
        .model = mesh->model,
        .mesh = mesh->mesh,
        .material_base_id = binding->material_base_id,
        .material_idx = binding->material_idx,
        .flags = emitter->instance->flags | binding->flags,
        .layer_mask = emitter->instance->visibility_mask,
    };
}

static Mel_Gpu_Pipeline* mel__scene_forward_get_sprite_pipeline(Mel_Material_Base* mat,
                                                                Mel_Gpu_Format target_format)
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

static void scene_forward_scene_init(Mel_Render_Pipeline_Scene* self, Mel_Render_Manager* mgr, Mel_Gpu_Device* dev)
{
    (void)self;
    (void)mgr;
    (void)dev;
}

static void scene_forward_scene_sync(Mel_Render_Pipeline_Scene* self, Mel_Render_Manager* mgr)
{
    Scene_Forward_Scene_Data* data = mel_pipeline_scene_instance(self);
    u64 serial = mel_mgr_mutation_serial(mgr);

    if (data->synced_serial != serial)
    {
        const Mel_Render_Instance* instances = mel_mgr_instances(mgr);
        u32 instance_count = mel_mgr_count(mgr);
        u32 base_count = mel_material_base_count();
        u32 sprite_counts[MEL_MATERIAL_BASE_MAX] = {0};
        u32 mesh_counts[MEL_MATERIAL_BASE_MAX] = {0};
        u32 sprite_count = 0;
        u32 sprite_range_count = 0;
        u32 mesh_count = 0;
        u32 mesh_range_count = 0;
        Mel_Scene_Forward_Emitter emitter = {
            .data = data,
            .mode = SCENE_FORWARD_EMIT_MODE_COUNT,
            .base_count = base_count,
            .sprite_counts = sprite_counts,
            .mesh_counts = mesh_counts,
        };

        for (u32 i = 0; i < instance_count; i++)
        {
            const Mel_Render_Instance* instance = &instances[i];
            if (instance->source == nullptr || instance->source->type == nullptr)
                continue;
            if (instance->source->type->scene_forward_emit == nullptr)
                continue;

            emitter.instance = instance;
            Mel_Render_Handle h = {
                .idx = mgr->packed_to_sparse[i],
                .gen = mgr->generations[mgr->packed_to_sparse[i]],
            };
            emitter.material_bindings = mel_mgr_get_material_bindings(mgr, h, &emitter.material_binding_count);
            instance->source->type->scene_forward_emit(instance->source, h, instance, &emitter);
        }

        for (u32 base_id = 0; base_id < base_count && base_id < MEL_MATERIAL_BASE_MAX; base_id++)
        {
            if (sprite_counts[base_id] > 0)
            {
                sprite_range_count++;
                sprite_count += sprite_counts[base_id];
            }
            if (mesh_counts[base_id] > 0)
            {
                mesh_range_count++;
                mesh_count += mesh_counts[base_id];
            }
        }

        scene_forward_scene_ensure_sprite_capacity(data, self->alloc, sprite_count, sprite_range_count);
        scene_forward_scene_ensure_mesh_capacity(data, self->alloc, mesh_count, mesh_range_count);

        data->sprite_count = sprite_count;
        data->sprite_range_count = 0;
        data->mesh_item_count = mesh_count;
        data->mesh_range_count = 0;

        u32 sprite_starts[MEL_MATERIAL_BASE_MAX] = {0};
        u32 mesh_starts[MEL_MATERIAL_BASE_MAX] = {0};
        u32 cursor = 0;
        for (u32 base_id = 0; base_id < base_count && base_id < MEL_MATERIAL_BASE_MAX; base_id++)
        {
            if (sprite_counts[base_id] == 0)
                continue;

            data->sprite_ranges[data->sprite_range_count++] = (Scene_Forward_Sprite_Range){
                .group = base_id,
                .start = cursor,
                .count = sprite_counts[base_id],
            };
            sprite_starts[base_id] = cursor;
            cursor += sprite_counts[base_id];
        }

        cursor = 0;
        for (u32 base_id = 0; base_id < base_count && base_id < MEL_MATERIAL_BASE_MAX; base_id++)
        {
            if (mesh_counts[base_id] == 0)
                continue;

            data->mesh_ranges[data->mesh_range_count++] = (Scene_Forward_Mesh_Range){
                .group = base_id,
                .start = cursor,
                .count = mesh_counts[base_id],
            };
            mesh_starts[base_id] = cursor;
            cursor += mesh_counts[base_id];
        }

        u32 sprite_fill[MEL_MATERIAL_BASE_MAX] = {0};
        u32 mesh_fill[MEL_MATERIAL_BASE_MAX] = {0};
        for (u32 base_id = 0; base_id < base_count && base_id < MEL_MATERIAL_BASE_MAX; base_id++)
        {
            sprite_fill[base_id] = sprite_starts[base_id];
            mesh_fill[base_id] = mesh_starts[base_id];
        }

        emitter.mode = SCENE_FORWARD_EMIT_MODE_FILL;
        emitter.sprite_fill = sprite_fill;
        emitter.mesh_fill = mesh_fill;

        for (u32 i = 0; i < instance_count; i++)
        {
            const Mel_Render_Instance* instance = &instances[i];
            if (instance->source == nullptr || instance->source->type == nullptr)
                continue;
            if (instance->source->type->scene_forward_emit == nullptr)
                continue;

            emitter.instance = instance;
            Mel_Render_Handle h = {
                .idx = mgr->packed_to_sparse[i],
                .gen = mgr->generations[mgr->packed_to_sparse[i]],
            };
            emitter.material_bindings = mel_mgr_get_material_bindings(mgr, h, &emitter.material_binding_count);
            instance->source->type->scene_forward_emit(instance->source, h, instance, &emitter);
        }

        if (sprite_count > 0)
        {
            scene_forward_scene_upload_buffer(&data->sprite_transform_buffer, self->dev,
                (u64)sprite_count * sizeof(Mel_Render_Transform_2D), data->sprite_transforms);
            scene_forward_scene_upload_buffer(&data->sprite_info_buffer, self->dev,
                (u64)sprite_count * sizeof(Mel_Render_Sprite_Info), data->sprite_infos);
            scene_forward_scene_upload_buffer(&data->sprite_draw_order_buffer, self->dev,
                (u64)sprite_count * sizeof(u32), data->sprite_draw_order);
        }

        data->synced_serial = serial;
    }

    for (u32 i = 0; i < data->mesh_range_count; i++)
    {
        Mel_Material_Base* base = mel_material_base_get(data->mesh_ranges[i].group);
        if (base == nullptr || base->instance_count == 0)
            continue;
        mel_material_base_upload_dirty(data->mesh_ranges[i].group, self->dev);
    }

    for (u32 i = 0; i < data->sprite_range_count; i++)
        mel_material_base_upload_dirty(data->sprite_ranges[i].group, self->dev);
}

static void scene_forward_view_init(Mel_Render_Pipeline* self, Mel_Render_View* view, Mel_Render_Pipeline_Scene* scene)
{
    (void)self;
    (void)view;

    Scene_Forward_View_Data* data = mel_pipeline_instance(self);
    assert(data != nullptr);

    data->mesh_strategy = scene_forward_pick_mesh_strategy(scene->dev);
    data->sprite_strategy = scene_forward_pick_sprite_strategy(scene->dev);
}

static void scene_forward_begin_frame(Mel_Render_Pipeline* self, Mel_Render_View* view, Mel_Render_Pipeline_Scene* scene)
{
    (void)self;
    (void)view;
    (void)scene;
}

static void scene_forward_begin_rendering(Scene_Forward_View_Data* data, Mel_Render_Draw_Ctx* ctx)
{
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
}

static void scene_forward_draw_meshes(Mel_Render_Pipeline* self,
                                      Scene_Forward_Scene_Data* scene_data,
                                      Mel_Render_Draw_Ctx* ctx)
{
    if (!s_mesh_ready || s_geometry_pool == nullptr)
        return;
    if (scene_data->mesh_item_count == 0)
        return;

    Scene_Forward_View_Data* data = mel_pipeline_instance(self);
    if (data->mesh_strategy != SCENE_FORWARD_MESH_STRATEGY_CLASSIC_VERTEX_PULLING)
        return;

    Mel_Texture_Table* texture_table = mel_texture_pool_get_table();
    if (texture_table == nullptr || texture_table->capacity == 0)
        return;

    Mel_Render_View* view = self->view;
    assert(view != nullptr);

    Mel_Gpu_Buffer* vert_buf = mel_geometry_pool_vertex_buffer(s_geometry_pool);
    Mel_Gpu_Buffer* idx_buf = mel_geometry_pool_index_buffer_u32(s_geometry_pool);
    if (vert_buf == nullptr || idx_buf == nullptr)
        return;

    Mel_Mat4 vp = mel_mat4_mul(view->camera.projection, view->camera.view);
    Mel_Mat4 inv_view = mel_mat4_inverse(view->camera.view);
    Scene_Forward_Mesh_View_Params view_params = {
        .view_projection = vp,
        .camera_position = mel_vec4(inv_view.m[0][3], inv_view.m[1][3], inv_view.m[2][3], 1.0f),
        .light_direction = mel_vec4(0.3f, 1.0f, 0.5f, 0.0f),
    };
    scene_forward_scene_upload_buffer(&data->mesh_view_buffer, s_dev, sizeof(view_params), &view_params);
    Mel_Gpu_Pipeline* bound_pipeline = nullptr;
    u32 bound_cull_mode = 0xFFFFFFFFu;

    for (u32 range_index = 0; range_index < scene_data->mesh_range_count; range_index++)
    {
        Scene_Forward_Mesh_Range range = scene_data->mesh_ranges[range_index];
        Mel_Material_Base* base = mel_material_base_get(range.group);
        if (base == nullptr || !(base->compat & MEL_COMPAT_FORWARD))
            continue;
        if (base->instance_count == 0)
            continue;

        Mel_Gpu_Buffer* mat_buf = mel_material_base_param_buffer(range.group);
        if (mat_buf == nullptr || mat_buf->_handle == nullptr)
            continue;

        void* desc = mel_gpu_pipeline_alloc_descriptor(&s_mesh_pipeline, s_dev);
        assert(desc != nullptr);

        mel_gpu_pipeline_write_buffer_binding(&s_mesh_pipeline, s_dev, desc, 0,
            vert_buf, 0, vert_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
        mel_gpu_pipeline_write_buffer_binding(&s_mesh_pipeline, s_dev, desc, 1,
            idx_buf, 0, idx_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
        mel_gpu_pipeline_write_buffer_binding(&s_mesh_pipeline, s_dev, desc, 2,
            mat_buf, 0, mat_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
        mel_gpu_pipeline_write_buffer_binding(&s_mesh_pipeline, s_dev, desc, 3,
            &data->mesh_view_buffer, 0, data->mesh_view_buffer.size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);

        for (u32 i = range.start; i < range.start + range.count; i++)
        {
            Scene_Forward_Mesh_Item* item = &scene_data->mesh_items[i];
            if (item->flags & MEL_RF_HIDDEN)
                continue;

            Mel_Gpu_Pipeline* mesh_pipeline = &s_mesh_pipeline;
            if (bound_pipeline != mesh_pipeline)
            {
                mel_gpu_cmd_bind_pipeline(ctx->cmd, mesh_pipeline);
                mel_gpu_cmd_bind_descriptor_set(ctx->cmd, mesh_pipeline, desc);
                mel_texture_table_bind(texture_table, ctx->cmd, mesh_pipeline->_layout, 1);
                bound_pipeline = mesh_pipeline;
            }

            u32 cull_mode = mel_material_base_get_cull_mode(item->material_base_id, item->material_idx);
            if (bound_cull_mode != cull_mode)
            {
                mel_gpu_cmd_set_cull_mode(ctx->cmd, cull_mode);
                bound_cull_mode = cull_mode;
            }

            Mel_Geometry_Region region = mel_geometry_pool_region(s_geometry_pool, item->mesh);
            Mel_Mat3 model_3x3 = mel_mat3_from_mat4(item->model);
            Mel_Mat3 normal_3x3 = mel_mat3_transpose(mel_mat3_inverse(model_3x3));

            Forward3D_Push pc = {
                .model_row0 = mel_vec4(item->model.m[0][0], item->model.m[0][1], item->model.m[0][2], item->model.m[0][3]),
                .model_row1 = mel_vec4(item->model.m[1][0], item->model.m[1][1], item->model.m[1][2], item->model.m[1][3]),
                .model_row2 = mel_vec4(item->model.m[2][0], item->model.m[2][1], item->model.m[2][2], item->model.m[2][3]),
                .normal_row0 = mel_vec4(normal_3x3.m[0][0], normal_3x3.m[0][1], normal_3x3.m[0][2], 0.0f),
                .normal_row1 = mel_vec4(normal_3x3.m[1][0], normal_3x3.m[1][1], normal_3x3.m[1][2], 0.0f),
                .normal_row2 = mel_vec4(normal_3x3.m[2][0], normal_3x3.m[2][1], normal_3x3.m[2][2], 0.0f),
                .vertex_offset = region.vertex_offset,
                .index_offset = region.index_offset,
                .material_idx = item->material_idx,
            };

            mel_gpu_cmd_push_constants(ctx->cmd, mesh_pipeline,
                MEL_GPU_SHADER_STAGE_VERTEX | MEL_GPU_SHADER_STAGE_FRAGMENT, 0, sizeof(pc), &pc);
            mel_gpu_cmd_draw(ctx->cmd, region.index_count, 1, 0, 0);
        }
    }
}

static void scene_forward_draw_sprites(Mel_Render_Pipeline* self,
                                       Scene_Forward_Scene_Data* scene_data,
                                       Mel_Render_Draw_Ctx* ctx)
{
    if (scene_data->sprite_count == 0)
        return;

    Scene_Forward_View_Data* data = mel_pipeline_instance(self);
    if (data->sprite_strategy != SCENE_FORWARD_SPRITE_STRATEGY_BINDLESS_SPRITES)
    {
        assert(false && "Classic sprite execution requires a dedicated non-bindless shader variant that does not yet exist");
        return;
    }

    Mel_Texture_Table* texture_table = mel_texture_pool_get_table();
    if (texture_table == nullptr || texture_table->capacity == 0)
        return;

    Mel_Render_View* view = self->view;
    assert(view != nullptr);

    Mel_Gpu_Buffer* transform_buf = &scene_data->sprite_transform_buffer;
    Mel_Gpu_Buffer* info_buf = &scene_data->sprite_info_buffer;
    Mel_Gpu_Buffer* draw_order_buf = &scene_data->sprite_draw_order_buffer;

    for (u32 i = 0; i < scene_data->sprite_range_count; i++)
    {
        Scene_Forward_Sprite_Range range = scene_data->sprite_ranges[i];
        Mel_Material_Base* mat = mel_material_base_get(range.group);
        if (mat == nullptr || !mat->shader_ready)
            continue;

        Mel_Gpu_Pipeline* gpu_pipe = mel__scene_forward_get_sprite_pipeline(mat, ctx->target_format);
        mel_gpu_cmd_bind_pipeline(ctx->cmd, gpu_pipe);

        void* desc = mel_gpu_pipeline_alloc_descriptor(gpu_pipe, s_dev);
        assert(desc != nullptr);

        mel_gpu_pipeline_write_buffer_binding(gpu_pipe, s_dev, desc, 0,
            transform_buf, 0, transform_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
        mel_gpu_pipeline_write_buffer_binding(gpu_pipe, s_dev, desc, 1,
            info_buf, 0, info_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);

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
}

static void scene_forward_draw(Mel_Render_Pipeline* self,
                               Mel_Render_Pipeline_Scene* scene,
                               Mel_Render_Manager* mgr,
                               Mel_Render_Draw_Ctx* ctx)
{
    assert(self != nullptr);
    assert(scene != nullptr);
    assert(ctx != nullptr);
    assert(ctx->cmd != nullptr);
    (void)mgr;

    Scene_Forward_Scene_Data* scene_data = mel_pipeline_scene_instance(scene);
    if (scene_data->mesh_item_count == 0 && scene_data->sprite_count == 0)
        return;

    Scene_Forward_View_Data* data = mel_pipeline_instance(self);
    assert(data != nullptr);

    scene_forward_begin_rendering(data, ctx);
    scene_forward_draw_meshes(self, scene_data, ctx);
    scene_forward_draw_sprites(self, scene_data, ctx);
    mel_gpu_cmd_end_rendering(ctx->cmd);
}

static void scene_forward_scene_shutdown(Mel_Render_Pipeline_Scene* self)
{
    Scene_Forward_Scene_Data* data = mel_pipeline_scene_instance(self);

    if (data->sprite_transform_buffer._handle != nullptr)
        mel_gpu_buffer_shutdown(&data->sprite_transform_buffer, self->dev);
    if (data->sprite_info_buffer._handle != nullptr)
        mel_gpu_buffer_shutdown(&data->sprite_info_buffer, self->dev);
    if (data->sprite_draw_order_buffer._handle != nullptr)
        mel_gpu_buffer_shutdown(&data->sprite_draw_order_buffer, self->dev);

    mel_dealloc(self->alloc, data->mesh_items);
    mel_dealloc(self->alloc, data->mesh_ranges);
    mel_dealloc(self->alloc, data->sprite_transforms);
    mel_dealloc(self->alloc, data->sprite_infos);
    mel_dealloc(self->alloc, data->sprite_draw_order);
    mel_dealloc(self->alloc, data->sprite_ranges);
}

static void scene_forward_view_shutdown(Mel_Render_Pipeline* self)
{
    Scene_Forward_View_Data* d = mel_pipeline_instance(self);
    if (d->mesh_view_buffer._handle != nullptr)
        mel_gpu_buffer_shutdown(&d->mesh_view_buffer, s_dev);
    if (d->depth_image._handle != nullptr)
        mel_gpu_image_shutdown(&d->depth_image, s_dev);
}

void mel_pipeline_scene_forward_set_geometry_pool(Mel_Geometry_Pool* pool)
{
    assert(pool != nullptr);
    s_geometry_pool = pool;
}

static const Mel_Render_Pipeline_Type s_scene_forward_type = {
    .name = { .data = (u8*)"scene_forward", .len = 13 },
    .scene_init = scene_forward_scene_init,
    .scene_sync = scene_forward_scene_sync,
    .scene_shutdown = scene_forward_scene_shutdown,
    .scene_size = sizeof(Scene_Forward_Scene_Data),
    .view_init = scene_forward_view_init,
    .begin_frame = scene_forward_begin_frame,
    .draw = scene_forward_draw,
    .view_shutdown = scene_forward_view_shutdown,
    .instance_size = sizeof(Scene_Forward_View_Data),
};

static void mel__scene_forward_compile(void* data)
{
    (void)data;

    mel_gpu_shader_load(&s_mesh_shader, .path = S8("shaders/mesh_forward.slang"), .dev = s_dev);

    Mel_Texture_Table* texture_table = mel_texture_pool_get_table();
    assert(texture_table != nullptr);

    Mel_Gpu_Descriptor_Binding bindings[] = {
        { .binding = 0, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
        { .binding = 1, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
        { .binding = 2, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX | MEL_GPU_SHADER_STAGE_FRAGMENT },
        { .binding = 3, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX | MEL_GPU_SHADER_STAGE_FRAGMENT },
    };
    void* extra_layouts[] = { texture_table->layout._layout };

    mel_gpu_pipeline_init(&s_mesh_pipeline, s_dev,
        .shader = &s_mesh_shader,
        .color_format = MEL_GPU_FORMAT_B8G8R8A8_SRGB,
        .depth_format = MEL_GPU_FORMAT_D32_SFLOAT,
        .blend_mode = MEL_GPU_BLEND_NONE,
        .cull_mode = MEL_GPU_CULL_BACK,
        .dynamic_cull_mode = true,
        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .depth_test = true,
        .depth_write = true,
        .push_constant_size = sizeof(Forward3D_Push),
        .push_constant_stages = MEL_GPU_SHADER_STAGE_VERTEX | MEL_GPU_SHADER_STAGE_FRAGMENT,
        .descriptor_bindings = bindings,
        .descriptor_binding_count = 4,
        .extra_set_layouts = extra_layouts,
        .extra_set_layout_count = 1,
        .max_descriptor_sets = 16);

    s_mesh_ready = true;
}

static void mel__scene_forward_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    s_dev = e->dev;

    if (!s_scene_forward_registered)
    {
        mel_pipeline_register(&s_scene_forward_type);
        mel_event_channel_fire(&mel_pipeline_scene_forward_ready, nullptr);
        s_scene_forward_registered = true;
    }

    mel_job_run(e->phase_counter, mel__scene_forward_compile, e->phase_counter);
}

static void mel__scene_forward_on_shutdown(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;

    if (s_mesh_ready)
    {
        mel_gpu_pipeline_shutdown(&s_mesh_pipeline, s_dev);
        mel_gpu_shader_shutdown(&s_mesh_shader, s_dev);
        s_mesh_ready = false;
    }
}

static void mel__scene_forward_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__scene_forward_on_gpu_ready, nullptr);
    mel_event_channel_on(&mel_shutdown_begin, mel__scene_forward_on_shutdown, nullptr);
}

__attribute__((constructor))
static void mel__scene_forward_register(void)
{
    mel_event_channel_init(&mel_pipeline_scene_forward_ready, mel_alloc_heap());
    mel__boot_register_wire(mel__scene_forward_wire);
}

__attribute__((destructor))
static void mel__scene_forward_unregister(void)
{
    mel_event_channel_destroy(&mel_pipeline_scene_forward_ready);
}
