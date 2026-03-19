#include "render.source.ecs.h"
#include "render.source.type.h"
#include "render.manager.h"
#include "render.types.3d.h"
#include "render.ecs.delta.h"
#include "collection.hashmap.h"
#include "allocator.h"
#include "allocator.heap.h"

typedef struct {
    Mel_ECS_Delta delta;
    Mel_HashMap entity_to_handle;
    Mel_ECS_Source_On_Add on_add;
    Mel_ECS_Source_On_Modify on_modify;
    ecs_world_t* world;
    const Mel_Alloc* alloc;
} Mel_ECS_Source_Data;


static void* ecs_create_manager(Mel_Render_Source* self, Mel_Gpu_Device* dev, const Mel_Alloc* alloc)
{
    (void)self;
    Mel_Render_Manager* mgr = mel_alloc(alloc, sizeof(Mel_Render_Manager));
    Mel_Mgr_Pool_Desc pools[] = {
        { .item_size = sizeof(Mel_Render_Transform) },
        { .item_size = sizeof(Mel_Render_Bounds) },
        { .item_size = sizeof(Mel_Render_Info) },
    };
    mel_mgr_init(mgr, .dev = dev, .alloc = alloc, .pools = pools, .pool_count = MEL_3D_POOL_COUNT);
    return mgr;
}

static void ecs_destroy_manager(Mel_Render_Source* self, void* mgr)
{
    (void)self;
    Mel_Render_Manager* m = mgr;
    mel_mgr_shutdown(m);
    mel_dealloc(mel_alloc_heap(), m);
}

static void ecs_sync(Mel_Render_Source* self, void* mgr)
{
    Mel_ECS_Source_Data* data = mel_render_source_instance(self);
    Mel_Render_Manager* m = mgr;

    mel_ecs_delta_begin_frame(&data->delta);

    u32 removed_count = mel_ecs_delta_removed_count(&data->delta);
    const ecs_entity_t* removed = mel_ecs_delta_removed(&data->delta);
    for (u32 i = 0; i < removed_count; i++)
    {
        void* val = mel_hashmap_get(&data->entity_to_handle, (void*)(usize)removed[i]);
        if (val != nullptr)
        {
            Mel_Render_Handle h = mel_render_handle_unpack64((u64)(usize)val);
            mel_mgr_free(m, h);
            mel_hashmap_remove(&data->entity_to_handle, (void*)(usize)removed[i]);
        }
    }

    u32 added_count = mel_ecs_delta_added_count(&data->delta);
    const ecs_entity_t* added = mel_ecs_delta_added(&data->delta);
    for (u32 i = 0; i < added_count; i++)
    {
        Mel_Render_Handle h = mel_mgr_alloc(m, 0);
        u64 packed = mel_render_handle_pack64(h);
        mel_hashmap_put(&data->entity_to_handle,
            (void*)(usize)added[i], (void*)(usize)packed);

        if (data->on_add)
            data->on_add(self, m, data->world, added[i], h);
    }

    u32 modified_count = mel_ecs_delta_modified_count(&data->delta);
    const ecs_entity_t* modified = mel_ecs_delta_modified(&data->delta);
    for (u32 i = 0; i < modified_count; i++)
    {
        void* val = mel_hashmap_get(&data->entity_to_handle, (void*)(usize)modified[i]);
        if (val == nullptr)
            continue;

        Mel_Render_Handle h = mel_render_handle_unpack64((u64)(usize)val);

        if (data->on_modify)
            data->on_modify(self, m, data->world, modified[i], h);
    }
}

static void ecs_shutdown(Mel_Render_Source* self)
{
    Mel_ECS_Source_Data* data = mel_render_source_instance(self);
    mel_ecs_delta_shutdown(&data->delta);
    mel_hashmap_free(&data->entity_to_handle);
}

const Mel_Render_Source_Type mel_source_ecs_type = {
    .name = { .data = (u8*)"ecs", .len = 3 },
    .create_manager = ecs_create_manager,
    .destroy_manager = ecs_destroy_manager,
    .sync = ecs_sync,
    .shutdown = ecs_shutdown,
    .instance_size = sizeof(Mel_ECS_Source_Data),
};

Mel_Render_Source* mel_source_ecs_create_opt(Mel_Source_ECS_Opt opt)
{
    assert(opt.world != nullptr);

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    Mel_Render_Source* source = mel_render_source_create(
        .type = &mel_source_ecs_type,
        .alloc = alloc);

    Mel_ECS_Source_Data* data = mel_render_source_instance(source);
    data->world = opt.world;
    data->on_add = opt.on_add;
    data->on_modify = opt.on_modify;
    data->alloc = alloc;

    mel_ecs_delta_init(&data->delta,
        .world = opt.world,
        .components = {
            opt.components[0], opt.components[1], opt.components[2], opt.components[3],
            opt.components[4], opt.components[5], opt.components[6], opt.components[7],
        },
        .alloc = alloc);

    mel_hashmap_init(&data->entity_to_handle,
        mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    return source;
}

Mel_Render_Handle mel_source_ecs_handle_for_entity(Mel_Render_Source* source, ecs_entity_t entity)
{
    assert(source != nullptr);
    assert(source->type == &mel_source_ecs_type);

    Mel_ECS_Source_Data* data = mel_render_source_instance(source);
    void* val = mel_hashmap_get(&data->entity_to_handle, (void*)(usize)entity);
    if (val == nullptr)
        return MEL_RENDER_HANDLE_NONE;

    return mel_render_handle_unpack64((u64)(usize)val);
}
