#include "render.source.ecs.2d.h"
#include "render.source.type.h"
#include "render.manager.2d.h"
#include "render.ecs.delta.h"
#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"
#include "collection.hashmap.h"
#include "collection.slotmap.fwd.h"
#include "allocator.h"
#include "allocator.heap.h"

typedef struct {
    Mel_ECS_Delta delta;
    Mel_HashMap entity_to_handle;
    ecs_world_t* world;
    const Mel_Alloc* alloc;
} Mel_ECS_2D_Source_Data;

static void sync_entity(Mel_Render_Manager_2D* mgr, ecs_world_t* world,
                         ecs_entity_t entity, Mel_Render_Handle_2D h)
{
    const Mel_CTransform* t = ecs_get(world, entity, Mel_CTransform);
    const Mel_Sprite* s = ecs_get(world, entity, Mel_Sprite);

    Mel_Render_Transform_2D transform = {
        .pos = t ? t->pos : MEL_VEC2_ZERO,
        .scale = s ? s->size : MEL_VEC2_ONE,
        .rotation = 0,
        .depth = 0,
        .flags = 0,
    };

    Mel_Render_Bounds_2D bounds = {
        .center = t ? t->pos : MEL_VEC2_ZERO,
        .half_extents = s ? mel_vec2(s->size.x * 0.5f, s->size.y * 0.5f) : MEL_VEC2_ZERO,
    };

    Mel_Render_Sprite_Info info = {
        .uv = s ? s->uv : mel_rect(0, 0, 1, 1),
        .color = s ? s->color : MEL_VEC4_ONE,
        .texture_idx = 0,
        .material_base_id = 0,
        .layer = 0,
    };

    mel_mgr_2d_set_transform(mgr, h, transform);
    mel_mgr_2d_set_bounds(mgr, h, bounds);
    mel_mgr_2d_set_sprite_info(mgr, h, info);
}

static void* ecs_2d_create_manager(Mel_Render_Source* self, Mel_Gpu_Device* dev, const Mel_Alloc* alloc)
{
    (void)self;
    Mel_Render_Manager_2D* mgr = mel_alloc(alloc, sizeof(Mel_Render_Manager_2D));
    mel_mgr_2d_init(mgr, .dev = dev, .alloc = alloc);
    return mgr;
}

static void ecs_2d_destroy_manager(Mel_Render_Source* self, void* mgr)
{
    (void)self;
    Mel_Render_Manager_2D* m = mgr;
    mel_mgr_2d_shutdown(m);
    mel_dealloc(mel_alloc_heap(), m);
}

static void ecs_2d_sync(Mel_Render_Source* self, void* mgr)
{
    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(self);
    Mel_Render_Manager_2D* m = mgr;

    u32 removed_count = mel_ecs_delta_removed_count(&data->delta);
    const ecs_entity_t* removed = mel_ecs_delta_removed(&data->delta);
    for (u32 i = 0; i < removed_count; i++)
    {
        void* val = mel_hashmap_get(&data->entity_to_handle, (void*)(usize)removed[i]);
        if (val != nullptr)
        {
            Mel_Render_Handle_2D h;
            h.handle = mel_slotmap_handle_unpack64((u64)(usize)val);
            mel_mgr_2d_free(m, h);
            mel_hashmap_remove(&data->entity_to_handle, (void*)(usize)removed[i]);
        }
    }

    u32 added_count = mel_ecs_delta_added_count(&data->delta);
    const ecs_entity_t* added = mel_ecs_delta_added(&data->delta);
    for (u32 i = 0; i < added_count; i++)
    {
        Mel_Render_Handle_2D h = mel_mgr_2d_alloc(m);
        u64 packed = mel_slotmap_handle_pack64(h.handle);
        mel_hashmap_put(&data->entity_to_handle,
            (void*)(usize)added[i], (void*)(usize)packed);

        sync_entity(m, data->world, added[i], h);
    }

    u32 modified_count = mel_ecs_delta_modified_count(&data->delta);
    const ecs_entity_t* modified = mel_ecs_delta_modified(&data->delta);
    for (u32 i = 0; i < modified_count; i++)
    {
        void* val = mel_hashmap_get(&data->entity_to_handle, (void*)(usize)modified[i]);
        if (val == nullptr)
            continue;

        Mel_Render_Handle_2D h;
        h.handle = mel_slotmap_handle_unpack64((u64)(usize)val);

        sync_entity(m, data->world, modified[i], h);
    }

    mel_ecs_delta_begin_frame(&data->delta);
}

static void ecs_2d_shutdown(Mel_Render_Source* self)
{
    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(self);
    mel_ecs_delta_shutdown(&data->delta);
    mel_hashmap_free(&data->entity_to_handle);
}

const Mel_Render_Source_Type mel_source_ecs_2d_type = {
    .name = { .data = (u8*)"ecs_2d", .len = 6 },
    .create_manager = ecs_2d_create_manager,
    .destroy_manager = ecs_2d_destroy_manager,
    .sync = ecs_2d_sync,
    .shutdown = ecs_2d_shutdown,
    .instance_size = sizeof(Mel_ECS_2D_Source_Data),
};

Mel_Render_Source* mel_source_ecs_2d_create_opt(Mel_Source_ECS_2D_Opt opt)
{
    assert(opt.world != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    Mel_Render_Source* source = mel_render_source_create(
        .type = &mel_source_ecs_2d_type,
        .alloc = alloc);

    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(source);
    data->world = opt.world;
    data->alloc = alloc;

    mel_ecs_delta_init(&data->delta,
        .world = opt.world,
        .components = { ecs_id(Mel_CTransform), ecs_id(Mel_Sprite) },
        .alloc = alloc);

    mel_hashmap_init(&data->entity_to_handle,
        mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    return source;
}

Mel_Render_Handle_2D mel_source_ecs_2d_handle_for_entity(Mel_Render_Source* source, ecs_entity_t entity)
{
    assert(source != nullptr);
    assert(source->type == &mel_source_ecs_2d_type);

    Mel_ECS_2D_Source_Data* data = mel_render_source_instance(source);
    void* val = mel_hashmap_get(&data->entity_to_handle, (void*)(usize)entity);
    if (val == nullptr)
        return MEL_RENDER_HANDLE_2D_NULL;

    Mel_Render_Handle_2D h;
    h.handle = mel_slotmap_handle_unpack64((u64)(usize)val);
    return h;
}
