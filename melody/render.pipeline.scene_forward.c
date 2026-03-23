#include "render.pipeline.scene_forward.h"
#include "render.pipeline.h"
#include "render.viewport.h"
#include "render.manager.h"
#include "render.environment.h"
#include "render.scene.h"
#include "render.cull.h"
#include "render.source.type.h"
#include "render.types.2d.h"
#include "render.types.3d.h"
#include "render.sprite2d.shader.h"
#include "render.texture_table.h"
#include "render.material_base.h"
#include "render.response.h"
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
#include "gpu.texture.h"
#include "gpu.descriptor.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "math.mat3.h"
#include "math.mat4.h"
#include "async.job.h"

#include <assert.h>
#include <float.h>
#include <math.h>

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
    Mel_Vec4 model_row0;
    Mel_Vec4 model_row1;
    Mel_Vec4 model_row2;
    u32 vertex_offset;
    u32 index_offset;
    u32 material_idx;
    u32 _pad;
} Forward3D_Shadow_Push;

typedef struct {
    Mel_Mat4 view_projection;
    Mel_Mat4 shadow_view_projection;
    Mel_Vec4 camera_position;
    Mel_Vec4 environment_radiance;
    Mel_Vec4 shadow_params;
    u32 directional_light_count;
    u32 point_light_count;
    u32 shadow_directional_light_index;
    u32 _pad0;
} Scene_Forward_Mesh_View_Params;

typedef struct {
    Mel_Mat4 light_view_projection;
} Scene_Forward_Shadow_View_Params;

_Static_assert(sizeof(Forward3D_Push) == 112, "Forward3D_Push must be 112 bytes");
_Static_assert(sizeof(Forward3D_Shadow_Push) == 64, "Forward3D_Shadow_Push must be 64 bytes");
_Static_assert(sizeof(Scene_Forward_Mesh_View_Params) == 192, "Scene_Forward_Mesh_View_Params must be 192 bytes");
_Static_assert(sizeof(Scene_Forward_Shadow_View_Params) == 64, "Scene_Forward_Shadow_View_Params must be 64 bytes");

#define SCENE_FORWARD_MESH_STRATEGY_NONE                   0u
#define SCENE_FORWARD_MESH_STRATEGY_CLASSIC_VERTEX_PULLING 1u

#define SCENE_FORWARD_SPRITE_STRATEGY_NONE             0u
#define SCENE_FORWARD_SPRITE_STRATEGY_BINDLESS_SPRITES 1u
#define SCENE_FORWARD_SPRITE_STRATEGY_CLASSIC_SPRITES  2u

typedef struct {
    u32 mesh_strategy;
    u32 sprite_strategy;
    Mel_Gpu_Image depth_image;
    Mel_Gpu_Image shadow_image;
    Mel_Render_Target_Handle hdr_target;
    Mel_Render_Target_Handle response_ping;
    Mel_Render_Target_Handle response_pong;
    void* shadow_sampler;
    Mel_Gpu_Buffer mesh_view_buffer;
    Mel_Gpu_Buffer shadow_view_buffer;
    Mel_Gpu_Buffer mesh_directional_light_buffer;
    Mel_Gpu_Buffer mesh_point_light_buffer;
    u32* transparent_mesh_order;
    f32* transparent_mesh_depths;
    u32 transparent_mesh_capacity;
    u32 last_width;
    u32 last_height;
    u32 shadow_map_size;
} Scene_Forward_View_Data;

typedef struct {
    Mel_Mat4 model;
    Mel_Render_Bounds bounds;
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
    u32* transparent_mesh_items;
    u32 mesh_item_capacity;
    u32 mesh_range_capacity;
    u32 transparent_mesh_capacity;
    u32 mesh_item_count;
    u32 mesh_range_count;
    u32 transparent_mesh_count;

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

typedef struct {
    bool enabled;
    u32 directional_light_index;
    Mel_Mat4 view_projection;
    Mel_Vec4 params;
} Scene_Forward_Shadow_Setup;

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
    u32* transparent_mesh_count;
    u32* sprite_fill;
    u32* mesh_fill;
    u32* transparent_mesh_fill;
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
static Mel_Gpu_Shader s_shadow_shader;
static Mel_Gpu_Pipeline s_shadow_pipeline;
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

static u32 scene_forward_shadow_map_size(u32 w, u32 h)
{
    u32 max_dim = w > h ? w : h;
    if (max_dim <= 1280)
        return 1024;
    return 2048;
}

static void scene_forward_destroy_color_targets(Scene_Forward_View_Data* d)
{
    if (mel_render_target_alive(d->hdr_target))
        mel_render_target_destroy(d->hdr_target);
    if (mel_render_target_alive(d->response_ping))
        mel_render_target_destroy(d->response_ping);
    if (mel_render_target_alive(d->response_pong))
        mel_render_target_destroy(d->response_pong);

    d->hdr_target = MEL_RENDER_TARGET_HANDLE_NULL;
    d->response_ping = MEL_RENDER_TARGET_HANDLE_NULL;
    d->response_pong = MEL_RENDER_TARGET_HANDLE_NULL;
}

static void scene_forward_ensure_color_target(Mel_Render_Target_Handle* handle,
                                              u32 width,
                                              u32 height,
                                              Mel_Gpu_Format format,
                                              const Mel_Alloc* alloc)
{
    if (mel_render_target_alive(*handle))
    {
        Mel_Render_Target* target = mel_render_target_get(*handle);
        if (target->width == width && target->height == height && target->format == format)
            return;
        mel_render_target_destroy(*handle);
        *handle = MEL_RENDER_TARGET_HANDLE_NULL;
    }

    *handle = mel_render_target_offscreen(
        .dev = s_dev,
        .width = width,
        .height = height,
        .format = format,
        .alloc = alloc);
}

static void scene_forward_ensure_response_targets(Scene_Forward_View_Data* d,
                                                  Mel_Render_Pipeline* self,
                                                  Mel_Render_Draw_Ctx* ctx,
                                                  u32 op_count)
{
    if (op_count == 0)
    {
        scene_forward_destroy_color_targets(d);
        return;
    }

    scene_forward_ensure_color_target(&d->hdr_target,
        ctx->target_width, ctx->target_height, MEL_GPU_FORMAT_R16G16B16A16_SFLOAT, self->alloc);

    if (op_count > 1)
        scene_forward_ensure_color_target(&d->response_ping,
            ctx->target_width, ctx->target_height, MEL_GPU_FORMAT_R16G16B16A16_SFLOAT, self->alloc);
    else if (mel_render_target_alive(d->response_ping))
    {
        mel_render_target_destroy(d->response_ping);
        d->response_ping = MEL_RENDER_TARGET_HANDLE_NULL;
    }

    if (op_count > 2)
        scene_forward_ensure_color_target(&d->response_pong,
            ctx->target_width, ctx->target_height, MEL_GPU_FORMAT_R16G16B16A16_SFLOAT, self->alloc);
    else if (mel_render_target_alive(d->response_pong))
    {
        mel_render_target_destroy(d->response_pong);
        d->response_pong = MEL_RENDER_TARGET_HANDLE_NULL;
    }
}

static void ensure_shadow_map(Scene_Forward_View_Data* d, u32 target_width, u32 target_height)
{
    u32 shadow_size = scene_forward_shadow_map_size(target_width, target_height);
    if (d->shadow_map_size == shadow_size &&
        d->shadow_image._handle != nullptr &&
        d->shadow_sampler != nullptr)
        return;

    if (d->shadow_sampler != nullptr)
    {
        mel_gpu_sampler_destroy(s_dev, d->shadow_sampler);
        d->shadow_sampler = nullptr;
    }
    if (d->shadow_image._handle != nullptr)
        mel_gpu_image_shutdown(&d->shadow_image, s_dev);

    mel_gpu_image_init(&d->shadow_image, s_dev,
        .width = shadow_size,
        .height = shadow_size,
        .format = MEL_GPU_FORMAT_D32_SFLOAT,
        .usage = MEL_GPU_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT | MEL_GPU_IMAGE_USAGE_SAMPLED,
        .aspect = MEL_GPU_ASPECT_DEPTH,
        .mip_levels = 1,
        .layer_count = 1);

    d->shadow_sampler = mel_gpu_sampler_create(s_dev,
        .nearest_filter = true,
        .address_mode_u = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE,
        .address_mode_v = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE,
        .address_mode_w = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE);
    d->shadow_map_size = shadow_size;
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
                                                     u32 range_capacity,
                                                     u32 transparent_capacity)
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

    if (transparent_capacity > data->transparent_mesh_capacity)
    {
        u32 new_capacity = data->transparent_mesh_capacity ? data->transparent_mesh_capacity : 16;
        while (new_capacity < transparent_capacity)
            new_capacity *= 2;

        data->transparent_mesh_items = data->transparent_mesh_items
            ? mel_realloc(alloc, data->transparent_mesh_items, (usize)new_capacity * sizeof(u32))
            : mel_alloc(alloc, (usize)new_capacity * sizeof(u32));
        data->transparent_mesh_capacity = new_capacity;
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
        if (mel_material_base_get_blend_mode(binding->material_base_id, binding->material_idx) != MEL_GPU_BLEND_NONE)
            (*emitter->transparent_mesh_count)++;
        return;
    }

    if (emitter->mode != SCENE_FORWARD_EMIT_MODE_FILL)
        return;

    u32 slot = emitter->mesh_fill[base_id]++;
    emitter->data->mesh_items[slot] = (Scene_Forward_Mesh_Item){
        .model = mesh->model,
        .bounds = mesh->bounds,
        .mesh = mesh->mesh,
        .material_base_id = binding->material_base_id,
        .material_idx = binding->material_idx,
        .flags = emitter->instance->flags | binding->flags,
        .layer_mask = emitter->instance->visibility_mask,
    };
    if (mel_material_base_get_blend_mode(binding->material_base_id, binding->material_idx) != MEL_GPU_BLEND_NONE)
        emitter->data->transparent_mesh_items[(*emitter->transparent_mesh_fill)++] = slot;
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
        u32 transparent_mesh_count = 0;
        Mel_Scene_Forward_Emitter emitter = {
            .data = data,
            .mode = SCENE_FORWARD_EMIT_MODE_COUNT,
            .base_count = base_count,
            .sprite_counts = sprite_counts,
            .mesh_counts = mesh_counts,
            .transparent_mesh_count = &transparent_mesh_count,
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
        scene_forward_scene_ensure_mesh_capacity(data, self->alloc, mesh_count, mesh_range_count, transparent_mesh_count);

        data->sprite_count = sprite_count;
        data->sprite_range_count = 0;
        data->mesh_item_count = mesh_count;
        data->mesh_range_count = 0;
        data->transparent_mesh_count = transparent_mesh_count;

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
        u32 transparent_mesh_fill = 0;
        emitter.transparent_mesh_fill = &transparent_mesh_fill;

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

static void scene_forward_begin_shadow_rendering(Scene_Forward_View_Data* data, Mel_Render_Draw_Ctx* ctx)
{
    ensure_shadow_map(data, ctx->target_width, ctx->target_height);
    mel_gpu_cmd_transition_image(ctx->cmd, &data->shadow_image, MEL_GPU_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT);

    Mel_Gpu_Depth_Attachment depth_att = {
        ._image_view = data->shadow_image._view,
        .layout = MEL_GPU_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT,
        .load_op = MEL_GPU_LOAD_OP_CLEAR,
        .store_op = MEL_GPU_STORE_OP_STORE,
        .clear_depth = 1.0f,
    };

    mel_gpu_cmd_begin_rendering(ctx->cmd,
        .color_count = 0,
        .depth_attachment = &depth_att,
        .render_width = data->shadow_map_size,
        .render_height = data->shadow_map_size);

    mel_gpu_cmd_set_viewport(ctx->cmd,
        0, (f32)data->shadow_map_size, (f32)data->shadow_map_size, -(f32)data->shadow_map_size, 0, 1);
    mel_gpu_cmd_set_scissor(ctx->cmd, 0, 0, data->shadow_map_size, data->shadow_map_size);
}

static void scene_forward_view_ensure_transparent_capacity(Scene_Forward_View_Data* data,
                                                           const Mel_Alloc* alloc,
                                                           u32 capacity)
{
    if (capacity <= data->transparent_mesh_capacity)
        return;

    u32 new_capacity = data->transparent_mesh_capacity ? data->transparent_mesh_capacity : 16;
    while (new_capacity < capacity)
        new_capacity *= 2;

    data->transparent_mesh_order = data->transparent_mesh_order
        ? mel_realloc(alloc, data->transparent_mesh_order, (usize)new_capacity * sizeof(u32))
        : mel_alloc(alloc, (usize)new_capacity * sizeof(u32));
    data->transparent_mesh_depths = data->transparent_mesh_depths
        ? mel_realloc(alloc, data->transparent_mesh_depths, (usize)new_capacity * sizeof(f32))
        : mel_alloc(alloc, (usize)new_capacity * sizeof(f32));
    data->transparent_mesh_capacity = new_capacity;
}

static Mel_Vec3 scene_forward_transform_point(Mel_Mat4 m, Mel_Vec3 p)
{
    return mel_vec3(
        m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3],
        m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3],
        m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3]);
}

static Mel_Vec3 scene_forward_transform_extents(Mel_Mat4 m, Mel_Vec3 e)
{
    return mel_vec3(
        fabsf(m.m[0][0]) * e.x + fabsf(m.m[0][1]) * e.y + fabsf(m.m[0][2]) * e.z,
        fabsf(m.m[1][0]) * e.x + fabsf(m.m[1][1]) * e.y + fabsf(m.m[1][2]) * e.z,
        fabsf(m.m[2][0]) * e.x + fabsf(m.m[2][1]) * e.y + fabsf(m.m[2][2]) * e.z);
}

static Mel_AABB scene_forward_world_bounds(const Scene_Forward_Mesh_Item* item)
{
    return (Mel_AABB){
        .center = scene_forward_transform_point(item->model, item->bounds.center),
        .extents = scene_forward_transform_extents(item->model, item->bounds.extents),
    };
}

static void scene_forward_aabb_min_max(Mel_AABB bounds, Mel_Vec3* out_min, Mel_Vec3* out_max)
{
    *out_min = mel_vec3(bounds.center.x - bounds.extents.x,
                        bounds.center.y - bounds.extents.y,
                        bounds.center.z - bounds.extents.z);
    *out_max = mel_vec3(bounds.center.x + bounds.extents.x,
                        bounds.center.y + bounds.extents.y,
                        bounds.center.z + bounds.extents.z);
}

static Mel_Vec3 scene_forward_clip_to_world(Mel_Mat4 inv_view_projection, f32 x, f32 y, f32 z)
{
    Mel_Vec4 clip = mel_vec4(x, y, z, 1.0f);
    Mel_Vec4 world = mel_mat4_mul_vec4(inv_view_projection, clip);
    f32 inv_w = fabsf(world.w) > 1e-6f ? 1.0f / world.w : 1.0f;
    return mel_vec3(world.x * inv_w, world.y * inv_w, world.z * inv_w);
}

static void scene_forward_frustum_corners(Mel_Mat4 inv_view_projection, Mel_Vec3 out_corners[8])
{
    out_corners[0] = scene_forward_clip_to_world(inv_view_projection, -1.0f, -1.0f, 0.0f);
    out_corners[1] = scene_forward_clip_to_world(inv_view_projection,  1.0f, -1.0f, 0.0f);
    out_corners[2] = scene_forward_clip_to_world(inv_view_projection, -1.0f,  1.0f, 0.0f);
    out_corners[3] = scene_forward_clip_to_world(inv_view_projection,  1.0f,  1.0f, 0.0f);
    out_corners[4] = scene_forward_clip_to_world(inv_view_projection, -1.0f, -1.0f, 1.0f);
    out_corners[5] = scene_forward_clip_to_world(inv_view_projection,  1.0f, -1.0f, 1.0f);
    out_corners[6] = scene_forward_clip_to_world(inv_view_projection, -1.0f,  1.0f, 1.0f);
    out_corners[7] = scene_forward_clip_to_world(inv_view_projection,  1.0f,  1.0f, 1.0f);
}

static bool scene_forward_mesh_item_visible(const Scene_Forward_Mesh_Item* item,
                                            const Mel_Frustum* frustum,
                                            u32 visibility_mask);

static Mel_Frustum scene_forward_extract_frustum(Mel_Mat4 view_projection)
{
    Mel_Frustum frustum = {0};
    frustum.planes[0] = mel_plane_normalize(mel_vec4_add(
        (Mel_Vec4){ .v = view_projection.rows[3] },
        (Mel_Vec4){ .v = view_projection.rows[0] }));
    frustum.planes[1] = mel_plane_normalize(mel_vec4_sub(
        (Mel_Vec4){ .v = view_projection.rows[3] },
        (Mel_Vec4){ .v = view_projection.rows[0] }));
    frustum.planes[2] = mel_plane_normalize(mel_vec4_add(
        (Mel_Vec4){ .v = view_projection.rows[3] },
        (Mel_Vec4){ .v = view_projection.rows[1] }));
    frustum.planes[3] = mel_plane_normalize(mel_vec4_sub(
        (Mel_Vec4){ .v = view_projection.rows[3] },
        (Mel_Vec4){ .v = view_projection.rows[1] }));
    frustum.planes[4] = mel_plane_normalize((Mel_Vec4){ .v = view_projection.rows[2] });
    frustum.planes[5] = mel_plane_normalize(mel_vec4_sub(
        (Mel_Vec4){ .v = view_projection.rows[3] },
        (Mel_Vec4){ .v = view_projection.rows[2] }));
    return frustum;
}

static Scene_Forward_Shadow_Setup scene_forward_shadow_setup(Mel_Render_View* view,
                                                             Mel_Render_Pipeline_Scene* scene,
                                                             Scene_Forward_Scene_Data* scene_data,
                                                             const Mel_Frustum* camera_frustum)
{
    Scene_Forward_Shadow_Setup result = {
        .enabled = false,
        .directional_light_index = 0xFFFFFFFFu,
    };

    u32 directional_light_count = 0;
    const Mel_Render_Scene_Directional_Light* directional_lights =
        mel_render_scene_directional_lights(scene->owner_scene, &directional_light_count);
    if (directional_lights == nullptr || directional_light_count == 0)
        return result;

    const Mel_Render_Scene_Directional_Light* shadow_light = nullptr;
    for (u32 i = 0; i < directional_light_count; i++)
    {
        if (directional_lights[i].flags & MEL_RENDER_SCENE_DIRECTIONAL_LIGHT_CAST_SHADOWS)
        {
            shadow_light = &directional_lights[i];
            result.directional_light_index = i;
            break;
        }
    }
    if (shadow_light == nullptr)
        return result;

    bool any_visible_bounds = false;
    Mel_Vec3 visible_world_min = mel_vec3(FLT_MAX, FLT_MAX, FLT_MAX);
    Mel_Vec3 visible_world_max = mel_vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (u32 i = 0; i < scene_data->mesh_item_count; i++)
    {
        const Scene_Forward_Mesh_Item* item = &scene_data->mesh_items[i];
        if (!scene_forward_mesh_item_visible(item, camera_frustum, view->camera.visibility_mask))
            continue;
        if (mel_material_base_get_blend_mode(item->material_base_id, item->material_idx) == MEL_GPU_BLEND_ALPHA)
            continue;

        Mel_Vec3 bmin, bmax;
        scene_forward_aabb_min_max(scene_forward_world_bounds(item), &bmin, &bmax);
        visible_world_min.x = fminf(visible_world_min.x, bmin.x);
        visible_world_min.y = fminf(visible_world_min.y, bmin.y);
        visible_world_min.z = fminf(visible_world_min.z, bmin.z);
        visible_world_max.x = fmaxf(visible_world_max.x, bmax.x);
        visible_world_max.y = fmaxf(visible_world_max.y, bmax.y);
        visible_world_max.z = fmaxf(visible_world_max.z, bmax.z);
        any_visible_bounds = true;
    }

    Mel_Mat4 view_projection = mel_mat4_mul(view->camera.projection, view->camera.view);
    Mel_Mat4 inv_view_projection = mel_mat4_inverse(view_projection);
    Mel_Vec3 frustum_corners[8];
    scene_forward_frustum_corners(inv_view_projection, frustum_corners);

    Mel_Vec3 frustum_center = mel_vec3(0.0f, 0.0f, 0.0f);
    for (u32 i = 0; i < 8; i++)
        frustum_center = mel_vec3_add(frustum_center, frustum_corners[i]);
    frustum_center = mel_vec3_scale(frustum_center, 1.0f / 8.0f);

    f32 frustum_radius_sq = 0.0f;
    for (u32 i = 0; i < 8; i++)
    {
        Mel_Vec3 d = mel_vec3_sub(frustum_corners[i], frustum_center);
        f32 dist_sq = mel_vec3_len_sq(d);
        if (dist_sq > frustum_radius_sq)
            frustum_radius_sq = dist_sq;
    }
    f32 frustum_radius = sqrtf(frustum_radius_sq);
    if (frustum_radius < 1.0f)
        frustum_radius = 1.0f;

    if (!any_visible_bounds)
        return result;

    Mel_Vec3 light_dir = mel_vec3(shadow_light->direction_intensity.x,
                                  shadow_light->direction_intensity.y,
                                  shadow_light->direction_intensity.z);
    if (mel_vec3_len_sq(light_dir) <= 0.000001f)
        light_dir = mel_vec3(0.0f, 1.0f, 0.0f);
    else
        light_dir = mel_vec3_normalize(light_dir);

    Mel_Vec3 up = mel_vec3(0.0f, 1.0f, 0.0f);
    if (fabsf(mel_vec3_dot(light_dir, up)) > 0.95f)
        up = mel_vec3(0.0f, 0.0f, 1.0f);

    f32 margin = frustum_radius * 0.15f + 2.0f;

    Mel_Vec3 eye = mel_vec3_add(frustum_center, mel_vec3_scale(light_dir, frustum_radius + margin));
    Mel_Mat4 light_view = mel_mat4_look_at(eye, frustum_center, up);

    Mel_Vec3 frustum_light_min = mel_vec3(FLT_MAX, FLT_MAX, FLT_MAX);
    Mel_Vec3 frustum_light_max = mel_vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (u32 i = 0; i < 8; i++)
    {
        Mel_Vec3 p = mel_mat4_mul_point(light_view, frustum_corners[i]);
        frustum_light_min.x = fminf(frustum_light_min.x, p.x);
        frustum_light_min.y = fminf(frustum_light_min.y, p.y);
        frustum_light_min.z = fminf(frustum_light_min.z, p.z);
        frustum_light_max.x = fmaxf(frustum_light_max.x, p.x);
        frustum_light_max.y = fmaxf(frustum_light_max.y, p.y);
        frustum_light_max.z = fmaxf(frustum_light_max.z, p.z);
    }

    Mel_Vec3 visible_corners[8] = {
        mel_vec3(visible_world_min.x, visible_world_min.y, visible_world_min.z),
        mel_vec3(visible_world_max.x, visible_world_min.y, visible_world_min.z),
        mel_vec3(visible_world_min.x, visible_world_max.y, visible_world_min.z),
        mel_vec3(visible_world_max.x, visible_world_max.y, visible_world_min.z),
        mel_vec3(visible_world_min.x, visible_world_min.y, visible_world_max.z),
        mel_vec3(visible_world_max.x, visible_world_min.y, visible_world_max.z),
        mel_vec3(visible_world_min.x, visible_world_max.y, visible_world_max.z),
        mel_vec3(visible_world_max.x, visible_world_max.y, visible_world_max.z),
    };
    f32 visible_light_min_z = FLT_MAX;
    f32 visible_light_max_z = -FLT_MAX;
    for (u32 i = 0; i < 8; i++)
    {
        Mel_Vec3 p = mel_mat4_mul_point(light_view, visible_corners[i]);
        visible_light_min_z = fminf(visible_light_min_z, p.z);
        visible_light_max_z = fmaxf(visible_light_max_z, p.z);
    }

    if (frustum_light_max.x - frustum_light_min.x < 0.01f) { frustum_light_min.x -= 0.5f; frustum_light_max.x += 0.5f; }
    if (frustum_light_max.y - frustum_light_min.y < 0.01f) { frustum_light_min.y -= 0.5f; frustum_light_max.y += 0.5f; }
    if (visible_light_max_z - visible_light_min_z < 0.01f) { visible_light_min_z -= 0.5f; visible_light_max_z += 0.5f; }

    Mel_Render_Target* effective_target = mel_render_view_effective_target(view);
    u32 shadow_map_size = effective_target
        ? scene_forward_shadow_map_size(effective_target->width, effective_target->height)
        : 1024u;
    f32 shadow_extent_x = frustum_light_max.x - frustum_light_min.x;
    f32 shadow_extent_y = frustum_light_max.y - frustum_light_min.y;
    f32 texel_size_x = shadow_extent_x / (f32)shadow_map_size;
    f32 texel_size_y = shadow_extent_y / (f32)shadow_map_size;
    f32 light_center_x = 0.5f * (frustum_light_min.x + frustum_light_max.x);
    f32 light_center_y = 0.5f * (frustum_light_min.y + frustum_light_max.y);
    if (texel_size_x > 0.0f)
        light_center_x = floorf(light_center_x / texel_size_x) * texel_size_x;
    if (texel_size_y > 0.0f)
        light_center_y = floorf(light_center_y / texel_size_y) * texel_size_y;
    frustum_light_min.x = light_center_x - shadow_extent_x * 0.5f;
    frustum_light_max.x = light_center_x + shadow_extent_x * 0.5f;
    frustum_light_min.y = light_center_y - shadow_extent_y * 0.5f;
    frustum_light_max.y = light_center_y + shadow_extent_y * 0.5f;

    Mel_Mat4 light_proj = mel_mat4_ortho(
        frustum_light_min.x, frustum_light_max.x,
        frustum_light_min.y, frustum_light_max.y,
        visible_light_min_z - margin, visible_light_max_z + margin);

    result.enabled = true;
    result.view_projection = mel_mat4_mul(light_proj, light_view);
    result.params = mel_vec4(shadow_light->shadow_params.x,
                             shadow_light->shadow_params.y,
                             shadow_light->shadow_params.z,
                             1.0f);
    return result;
}

static bool scene_forward_mesh_item_visible(const Scene_Forward_Mesh_Item* item,
                                            const Mel_Frustum* frustum,
                                            u32 visibility_mask)
{
    if (item->flags & MEL_RF_HIDDEN)
        return false;
    if ((item->layer_mask & visibility_mask) == 0)
        return false;

    Mel_AABB bounds = scene_forward_world_bounds(item);
    return mel_aabb_vs_frustum(bounds, frustum);
}

static void* scene_forward_mesh_write_descriptor(Mel_Gpu_Pipeline* pipeline,
                                                 Mel_Gpu_Buffer* vert_buf,
                                                 Mel_Gpu_Buffer* idx_buf,
                                                 Mel_Gpu_Buffer* mat_buf,
                                                 Mel_Gpu_Buffer* view_buf,
                                                 Mel_Gpu_Buffer* directional_light_buf,
                                                 Mel_Gpu_Buffer* point_light_buf,
                                                 Mel_Gpu_Image* shadow_image,
                                                 void* shadow_sampler)
{
    void* desc = mel_gpu_pipeline_alloc_descriptor(pipeline, s_dev);
    assert(desc != nullptr);

    mel_gpu_pipeline_write_buffer_binding(pipeline, s_dev, desc, 0,
        vert_buf, 0, vert_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_buffer_binding(pipeline, s_dev, desc, 1,
        idx_buf, 0, idx_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_buffer_binding(pipeline, s_dev, desc, 2,
        mat_buf, 0, mat_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_buffer_binding(pipeline, s_dev, desc, 3,
        view_buf, 0, view_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_buffer_binding(pipeline, s_dev, desc, 4,
        directional_light_buf, 0, directional_light_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_buffer_binding(pipeline, s_dev, desc, 5,
        point_light_buf, 0, point_light_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_texture_binding(pipeline, s_dev, desc, 6,
        shadow_image->_view, shadow_sampler);

    return desc;
}

static Mel_Gpu_Pipeline* scene_forward_get_mesh_pipeline(Mel_Gpu_Format color_format,
                                                         u32 blend_mode)
{
    Mel_Texture_Table* texture_table = mel_texture_pool_get_table();
    assert(texture_table != nullptr);

    Mel_Gpu_Descriptor_Binding bindings[] = {
        { .binding = 0, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
        { .binding = 1, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
        { .binding = 2, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX | MEL_GPU_SHADER_STAGE_FRAGMENT },
        { .binding = 3, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX | MEL_GPU_SHADER_STAGE_FRAGMENT },
        { .binding = 4, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_FRAGMENT },
        { .binding = 5, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_FRAGMENT },
        { .binding = 6, .type = MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_FRAGMENT },
    };
    void* extra_layouts[] = { texture_table->layout._layout };

    return mel_gpu_pipeline_cache_get(s_dev->pipeline_cache, s_dev,
        .shader = &s_mesh_shader,
        .color_format = color_format,
        .depth_format = MEL_GPU_FORMAT_D32_SFLOAT,
        .blend_mode = blend_mode,
        .cull_mode = MEL_GPU_CULL_BACK,
        .dynamic_cull_mode = true,
        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .depth_test = true,
        .depth_write = blend_mode == MEL_GPU_BLEND_NONE,
        .push_constant_size = sizeof(Forward3D_Push),
        .push_constant_stages = MEL_GPU_SHADER_STAGE_VERTEX | MEL_GPU_SHADER_STAGE_FRAGMENT,
        .descriptor_bindings = bindings,
        .descriptor_binding_count = 7,
        .extra_set_layouts = extra_layouts,
        .extra_set_layout_count = 1,
        .max_descriptor_sets = 16);
}

static void* scene_forward_shadow_write_descriptor(Mel_Gpu_Buffer* vert_buf,
                                                   Mel_Gpu_Buffer* idx_buf,
                                                   Mel_Gpu_Buffer* mat_buf,
                                                   Mel_Gpu_Buffer* shadow_view_buf)
{
    void* desc = mel_gpu_pipeline_alloc_descriptor(&s_shadow_pipeline, s_dev);
    assert(desc != nullptr);

    mel_gpu_pipeline_write_buffer_binding(&s_shadow_pipeline, s_dev, desc, 0,
        vert_buf, 0, vert_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_buffer_binding(&s_shadow_pipeline, s_dev, desc, 1,
        idx_buf, 0, idx_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_buffer_binding(&s_shadow_pipeline, s_dev, desc, 2,
        mat_buf, 0, mat_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);
    mel_gpu_pipeline_write_buffer_binding(&s_shadow_pipeline, s_dev, desc, 3,
        shadow_view_buf, 0, shadow_view_buf->size, MEL_GPU_DESCRIPTOR_STORAGE_BUFFER);

    return desc;
}

static void scene_forward_draw_meshes(Mel_Render_Pipeline* self,
                                      Mel_Render_Pipeline_Scene* scene,
                                      Scene_Forward_Scene_Data* scene_data,
                                      Mel_Render_Draw_Ctx* ctx,
                                      const Scene_Forward_Shadow_Setup* shadow_setup)
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
    Mel_Frustum frustum = scene_forward_extract_frustum(vp);
    Mel_Mat4 inv_view = mel_mat4_inverse(view->camera.view);
    u32 directional_light_count = 0;
    u32 point_light_count = 0;
    const Mel_Render_Scene_Directional_Light* directional_lights =
        mel_render_scene_directional_lights(scene->owner_scene, &directional_light_count);
    const Mel_Render_Scene_Point_Light* point_lights =
        mel_render_scene_point_lights(scene->owner_scene, &point_light_count);
    Mel_Render_Scene_Directional_Light directional_light_dummy = {0};
    Mel_Render_Scene_Point_Light point_light_dummy = {0};

    if (directional_light_count == 0)
        directional_lights = &directional_light_dummy;
    if (point_light_count == 0)
        point_lights = &point_light_dummy;

    Mel_Vec4 environment_radiance = mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Mel_Render_Environment_Handle environment_handle =
        mel_render_scene_environment(scene->owner_scene);
    if (mel_render_environment_alive(environment_handle))
    {
        Mel_Render_Environment* environment = mel_render_environment_get(environment_handle);
        if (environment->type == MEL_RENDER_ENVIRONMENT_CONSTANT)
            environment_radiance = environment->constant_radiance;
    }

    Scene_Forward_Mesh_View_Params view_params = {
        .view_projection = vp,
        .shadow_view_projection = shadow_setup->view_projection,
        .camera_position = mel_vec4(inv_view.m[0][3], inv_view.m[1][3], inv_view.m[2][3], 1.0f),
        .environment_radiance = environment_radiance,
        .shadow_params = shadow_setup->params,
        .directional_light_count = directional_light_count,
        .point_light_count = point_light_count,
        .shadow_directional_light_index = shadow_setup->enabled ? shadow_setup->directional_light_index : 0xFFFFFFFFu,
    };
    scene_forward_scene_upload_buffer(&data->mesh_view_buffer, s_dev, sizeof(view_params), &view_params);
    scene_forward_scene_upload_buffer(&data->mesh_directional_light_buffer, s_dev,
        sizeof(directional_lights[0]) * (directional_light_count ? directional_light_count : 1u),
        directional_lights);
    scene_forward_scene_upload_buffer(&data->mesh_point_light_buffer, s_dev,
        sizeof(point_lights[0]) * (point_light_count ? point_light_count : 1u),
        point_lights);
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

        Mel_Gpu_Pipeline* mesh_pipeline = scene_forward_get_mesh_pipeline(ctx->target_format, MEL_GPU_BLEND_NONE);
        void* desc = scene_forward_mesh_write_descriptor(mesh_pipeline,
            vert_buf, idx_buf, mat_buf,
            &data->mesh_view_buffer,
            &data->mesh_directional_light_buffer,
            &data->mesh_point_light_buffer,
            &data->shadow_image,
            data->shadow_sampler);

        for (u32 i = range.start; i < range.start + range.count; i++)
        {
            Scene_Forward_Mesh_Item* item = &scene_data->mesh_items[i];
            if (!scene_forward_mesh_item_visible(item, &frustum, view->camera.visibility_mask))
                continue;
            if (mel_material_base_get_blend_mode(item->material_base_id, item->material_idx) != MEL_GPU_BLEND_NONE)
                continue;

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

    if (scene_data->transparent_mesh_count == 0)
        return;

    scene_forward_view_ensure_transparent_capacity(data, self->alloc, scene_data->transparent_mesh_count);

    Mel_Vec3 camera_position = mel_vec3(inv_view.m[0][3], inv_view.m[1][3], inv_view.m[2][3]);
    u32 visible_transparent_count = 0;
    for (u32 i = 0; i < scene_data->transparent_mesh_count; i++)
    {
        u32 item_index = scene_data->transparent_mesh_items[i];
        Scene_Forward_Mesh_Item* item = &scene_data->mesh_items[item_index];
        if (!scene_forward_mesh_item_visible(item, &frustum, view->camera.visibility_mask))
            continue;

        Mel_Vec3 world_center = scene_forward_transform_point(item->model, item->bounds.center);
        Mel_Vec3 to_camera = mel_vec3_sub(world_center, camera_position);
        data->transparent_mesh_order[visible_transparent_count] = item_index;
        data->transparent_mesh_depths[visible_transparent_count] = mel_vec3_len_sq(to_camera);
        visible_transparent_count++;
    }

    for (u32 i = 1; i < visible_transparent_count; i++)
    {
        u32 item_index = data->transparent_mesh_order[i];
        f32 depth = data->transparent_mesh_depths[i];
        u32 j = i;
        while (j > 0 && data->transparent_mesh_depths[j - 1] < depth)
        {
            data->transparent_mesh_order[j] = data->transparent_mesh_order[j - 1];
            data->transparent_mesh_depths[j] = data->transparent_mesh_depths[j - 1];
            j--;
        }
        data->transparent_mesh_order[j] = item_index;
        data->transparent_mesh_depths[j] = depth;
    }

    u32 bound_material_base_id = 0xFFFFFFFFu;
    void* desc = nullptr;
    bound_pipeline = nullptr;
    bound_cull_mode = 0xFFFFFFFFu;

    for (u32 sorted_index = 0; sorted_index < visible_transparent_count; sorted_index++)
    {
        Scene_Forward_Mesh_Item* item = &scene_data->mesh_items[data->transparent_mesh_order[sorted_index]];

        u32 blend_mode = mel_material_base_get_blend_mode(item->material_base_id, item->material_idx);
        if (blend_mode != MEL_GPU_BLEND_ALPHA)
            continue;

        Mel_Material_Base* base = mel_material_base_get(item->material_base_id);
        if (base == nullptr || !(base->compat & MEL_COMPAT_FORWARD))
            continue;

        Mel_Gpu_Buffer* mat_buf = mel_material_base_param_buffer(item->material_base_id);
        if (mat_buf == nullptr || mat_buf->_handle == nullptr)
            continue;

        if (bound_material_base_id != item->material_base_id)
        {
            Mel_Gpu_Pipeline* mesh_pipeline = scene_forward_get_mesh_pipeline(ctx->target_format, MEL_GPU_BLEND_ALPHA);
            desc = scene_forward_mesh_write_descriptor(mesh_pipeline,
                vert_buf, idx_buf, mat_buf,
                &data->mesh_view_buffer,
                &data->mesh_directional_light_buffer,
                &data->mesh_point_light_buffer,
                &data->shadow_image,
                data->shadow_sampler);
            bound_material_base_id = item->material_base_id;
            bound_pipeline = nullptr;
        }

        Mel_Gpu_Pipeline* mesh_pipeline = scene_forward_get_mesh_pipeline(ctx->target_format, MEL_GPU_BLEND_ALPHA);
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

static void scene_forward_draw_shadow_map(Mel_Render_Pipeline* self,
                                          Mel_Render_Pipeline_Scene* scene,
                                          Scene_Forward_Scene_Data* scene_data,
                                          Mel_Render_Draw_Ctx* ctx,
                                          const Scene_Forward_Shadow_Setup* shadow_setup)
{
    if (!shadow_setup->enabled || !s_mesh_ready || s_geometry_pool == nullptr)
        return;
    if (scene_data->mesh_item_count == 0)
        return;

    Scene_Forward_View_Data* data = mel_pipeline_instance(self);
    Mel_Texture_Table* texture_table = mel_texture_pool_get_table();
    Mel_Gpu_Buffer* vert_buf = mel_geometry_pool_vertex_buffer(s_geometry_pool);
    Mel_Gpu_Buffer* idx_buf = mel_geometry_pool_index_buffer_u32(s_geometry_pool);
    if (texture_table == nullptr || vert_buf == nullptr || idx_buf == nullptr)
        return;

    Scene_Forward_Shadow_View_Params shadow_view = {
        .light_view_projection = shadow_setup->view_projection,
    };
    scene_forward_scene_upload_buffer(&data->shadow_view_buffer, s_dev, sizeof(shadow_view), &shadow_view);

    Mel_Frustum shadow_frustum = scene_forward_extract_frustum(shadow_setup->view_projection);
    scene_forward_begin_shadow_rendering(data, ctx);

    u32 bound_material_base_id = 0xFFFFFFFFu;
    u32 bound_cull_mode = 0xFFFFFFFFu;
    void* desc = nullptr;

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

        if (bound_material_base_id != range.group)
        {
            desc = scene_forward_shadow_write_descriptor(vert_buf, idx_buf, mat_buf, &data->shadow_view_buffer);
            bound_material_base_id = range.group;
            mel_gpu_cmd_bind_pipeline(ctx->cmd, &s_shadow_pipeline);
            mel_gpu_cmd_bind_descriptor_set(ctx->cmd, &s_shadow_pipeline, desc);
            mel_texture_table_bind(texture_table, ctx->cmd, s_shadow_pipeline._layout, 1);
            bound_cull_mode = 0xFFFFFFFFu;
        }

        for (u32 i = range.start; i < range.start + range.count; i++)
        {
            Scene_Forward_Mesh_Item* item = &scene_data->mesh_items[i];
            if (!scene_forward_mesh_item_visible(item, &shadow_frustum, self->view->camera.visibility_mask))
                continue;
            if (mel_material_base_get_blend_mode(item->material_base_id, item->material_idx) == MEL_GPU_BLEND_ALPHA)
                continue;

            u32 cull_mode = mel_material_base_get_cull_mode(item->material_base_id, item->material_idx);
            if (bound_cull_mode != cull_mode)
            {
                mel_gpu_cmd_set_cull_mode(ctx->cmd, cull_mode);
                bound_cull_mode = cull_mode;
            }

            Mel_Geometry_Region region = mel_geometry_pool_region(s_geometry_pool, item->mesh);
            Forward3D_Shadow_Push pc = {
                .model_row0 = mel_vec4(item->model.m[0][0], item->model.m[0][1], item->model.m[0][2], item->model.m[0][3]),
                .model_row1 = mel_vec4(item->model.m[1][0], item->model.m[1][1], item->model.m[1][2], item->model.m[1][3]),
                .model_row2 = mel_vec4(item->model.m[2][0], item->model.m[2][1], item->model.m[2][2], item->model.m[2][3]),
                .vertex_offset = region.vertex_offset,
                .index_offset = region.index_offset,
                .material_idx = item->material_idx,
            };

            mel_gpu_cmd_push_constants(ctx->cmd, &s_shadow_pipeline,
                MEL_GPU_SHADER_STAGE_VERTEX | MEL_GPU_SHADER_STAGE_FRAGMENT, 0, sizeof(pc), &pc);
            mel_gpu_cmd_draw(ctx->cmd, region.index_count, 1, 0, 0);
        }
    }

    mel_gpu_cmd_end_rendering(ctx->cmd);
    mel_gpu_cmd_transition_image(ctx->cmd, &data->shadow_image, MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY);
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
    u32 response_op_count = mel_render_view_response_op_count(self->view->self);
    scene_forward_ensure_response_targets(data, self, ctx, response_op_count);

    Mel_Mat4 vp = mel_mat4_mul(self->view->camera.projection, self->view->camera.view);
    Mel_Frustum camera_frustum = scene_forward_extract_frustum(vp);
    Scene_Forward_Shadow_Setup shadow_setup =
        scene_forward_shadow_setup(self->view, scene, scene_data, &camera_frustum);

    ensure_shadow_map(data, ctx->target_width, ctx->target_height);
    if (shadow_setup.enabled)
        scene_forward_draw_shadow_map(self, scene, scene_data, ctx, &shadow_setup);
    else
        mel_gpu_cmd_transition_image(ctx->cmd, &data->shadow_image, MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY);

    Mel_Render_Draw_Ctx scene_ctx = *ctx;
    if (response_op_count > 0)
    {
        Mel_Render_Target* hdr_target = mel_render_target_get(data->hdr_target);
        assert(hdr_target != nullptr);
        scene_ctx.target = hdr_target;
        scene_ctx.target_width = hdr_target->width;
        scene_ctx.target_height = hdr_target->height;
        scene_ctx.target_format = hdr_target->format;
    }

    scene_forward_begin_rendering(data, &scene_ctx);
    scene_forward_draw_meshes(self, scene, scene_data, &scene_ctx, &shadow_setup);
    scene_forward_draw_sprites(self, scene_data, &scene_ctx);
    mel_gpu_cmd_end_rendering(scene_ctx.cmd);

    if (response_op_count > 0)
    {
        Mel_Render_Response_Ctx response_ctx = {
            .view = self->view,
            .scene = scene->owner_scene,
            .draw_ctx = ctx,
        };

        mel_render_view_response_execute(self->view->self,
            &response_ctx,
            mel_render_target_get(data->hdr_target),
            mel_render_target_alive(data->response_ping) ? mel_render_target_get(data->response_ping) : nullptr,
            mel_render_target_alive(data->response_pong) ? mel_render_target_get(data->response_pong) : nullptr,
            ctx->target);
    }
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
    mel_dealloc(self->alloc, data->transparent_mesh_items);
    mel_dealloc(self->alloc, data->sprite_transforms);
    mel_dealloc(self->alloc, data->sprite_infos);
    mel_dealloc(self->alloc, data->sprite_draw_order);
    mel_dealloc(self->alloc, data->sprite_ranges);
}

static void scene_forward_view_shutdown(Mel_Render_Pipeline* self)
{
    Scene_Forward_View_Data* d = mel_pipeline_instance(self);
    mel_dealloc(self->alloc, d->transparent_mesh_order);
    mel_dealloc(self->alloc, d->transparent_mesh_depths);
    scene_forward_destroy_color_targets(d);
    if (d->mesh_view_buffer._handle != nullptr)
        mel_gpu_buffer_shutdown(&d->mesh_view_buffer, s_dev);
    if (d->shadow_view_buffer._handle != nullptr)
        mel_gpu_buffer_shutdown(&d->shadow_view_buffer, s_dev);
    if (d->mesh_directional_light_buffer._handle != nullptr)
        mel_gpu_buffer_shutdown(&d->mesh_directional_light_buffer, s_dev);
    if (d->mesh_point_light_buffer._handle != nullptr)
        mel_gpu_buffer_shutdown(&d->mesh_point_light_buffer, s_dev);
    if (d->depth_image._handle != nullptr)
        mel_gpu_image_shutdown(&d->depth_image, s_dev);
    if (d->shadow_sampler != nullptr)
        mel_gpu_sampler_destroy(s_dev, d->shadow_sampler);
    if (d->shadow_image._handle != nullptr)
        mel_gpu_image_shutdown(&d->shadow_image, s_dev);
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
    mel_gpu_shader_load(&s_shadow_shader, .path = S8("shaders/mesh_shadow.slang"), .dev = s_dev);

    Mel_Texture_Table* texture_table = mel_texture_pool_get_table();
    assert(texture_table != nullptr);

    Mel_Gpu_Descriptor_Binding shadow_bindings[] = {
        { .binding = 0, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
        { .binding = 1, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
        { .binding = 2, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_FRAGMENT },
        { .binding = 3, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = MEL_GPU_SHADER_STAGE_VERTEX },
    };
    void* extra_layouts[] = { texture_table->layout._layout };

    mel_gpu_pipeline_init(&s_shadow_pipeline, s_dev,
        .shader = &s_shadow_shader,
        .depth_format = MEL_GPU_FORMAT_D32_SFLOAT,
        .cull_mode = MEL_GPU_CULL_BACK,
        .dynamic_cull_mode = true,
        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .depth_test = true,
        .depth_write = true,
        .push_constant_size = sizeof(Forward3D_Shadow_Push),
        .push_constant_stages = MEL_GPU_SHADER_STAGE_VERTEX | MEL_GPU_SHADER_STAGE_FRAGMENT,
        .descriptor_bindings = shadow_bindings,
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
        mel_gpu_pipeline_shutdown(&s_shadow_pipeline, s_dev);
        mel_gpu_shader_shutdown(&s_shadow_shader, s_dev);
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
