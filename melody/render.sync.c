#include "render.sync.h"
#include "render.list.h"
#include "allocator.heap.h"

#include <assert.h>

static void mel__render_sync_on_set(ecs_iter_t* it)
{
    Mel_Render_Sync* sync = it->ctx;

    for (i32 i = 0; i < it->count; i++)
    {
        void* existing = mel_hashmap_get(&sync->entity_map,
            (void*)(usize)it->entities[i]);

        if (existing)
        {
            u32 entry_index = (u32)((usize)existing - 1);
            void* entry = mel_render_list_get(sync->list, entry_index);
            sync->write(entry, it, i, sync->user);

            if (sync->key)
            {
                u64 sort_key = sync->key(it, i, sync->user);
                mel_render_list_update_key(sync->list, entry_index, sort_key);
            }
            continue;
        }

        u64 sort_key = sync->key ? sync->key(it, i, sync->user) : 0;
        u32 entry_index = mel_render_list_insert(sync->list, sort_key);
        void* entry = mel_render_list_get(sync->list, entry_index);
        sync->write(entry, it, i, sync->user);

        mel_hashmap_put(&sync->entity_map,
            (void*)(usize)it->entities[i],
            (void*)(usize)(entry_index + 1));
    }
}

static void mel__render_sync_on_remove(ecs_iter_t* it)
{
    Mel_Render_Sync* sync = it->ctx;

    for (i32 i = 0; i < it->count; i++)
    {
        void* val = mel_hashmap_get(&sync->entity_map,
            (void*)(usize)it->entities[i]);
        if (val == nullptr) continue;

        u32 entry_index = (u32)((usize)val - 1);
        mel_render_list_remove(sync->list, entry_index);
        mel_hashmap_remove(&sync->entity_map,
            (void*)(usize)it->entities[i]);
    }
}

void mel_render_sync_init_opt(Mel_Render_Sync* sync, Mel_Render_Sync_Opt opt)
{
    assert(sync != nullptr);
    assert(opt.list != nullptr);
    assert(opt.world != nullptr);
    assert(opt.write != nullptr);
    assert(opt.components[0] != 0);

    *sync = (Mel_Render_Sync){0};
    sync->list = opt.list;
    sync->world = opt.world;
    sync->write = opt.write;
    sync->key = opt.key;
    sync->user = opt.user;

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    mel_hashmap_init(&sync->entity_map,
        mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    i32 term_count = 0;
    while (term_count < 8 && opt.components[term_count] != 0)
        term_count++;

    ecs_query_desc_t qdesc = {0};
    for (i32 i = 0; i < term_count; i++)
        qdesc.terms[i].id = opt.components[i];

    sync->query = ecs_query_init(opt.world, &qdesc);
    assert(sync->query != nullptr);

    ecs_observer_desc_t set_desc = {0};
    for (i32 i = 0; i < term_count; i++)
        set_desc.query.terms[i].id = opt.components[i];
    set_desc.events[0] = EcsOnSet;
    set_desc.callback = mel__render_sync_on_set;
    set_desc.ctx = sync;
    set_desc.yield_existing = true;

    sync->on_set = ecs_observer_init(opt.world, &set_desc);
    assert(sync->on_set != 0);

    ecs_observer_desc_t remove_desc = {0};
    for (i32 i = 0; i < term_count; i++)
        remove_desc.query.terms[i].id = opt.components[i];
    remove_desc.events[0] = EcsOnRemove;
    remove_desc.callback = mel__render_sync_on_remove;
    remove_desc.ctx = sync;

    sync->on_remove = ecs_observer_init(opt.world, &remove_desc);
    assert(sync->on_remove != 0);
}

void mel_render_sync_shutdown(Mel_Render_Sync* sync)
{
    assert(sync != nullptr);

    if (sync->world == nullptr) return;

    ecs_delete(sync->world, sync->on_set);
    ecs_delete(sync->world, sync->on_remove);
    ecs_query_fini(sync->query);

    mel_hashmap_foreach(&sync->entity_map, k, v, {
        (void)k;
        u32 entry_index = (u32)((usize)v - 1);
        mel_render_list_remove(sync->list, entry_index);
    });

    mel_hashmap_free(&sync->entity_map);

    *sync = (Mel_Render_Sync){0};
}

void mel_render_sync_update(Mel_Render_Sync* sync)
{
    assert(sync != nullptr);

    ecs_iter_t it = ecs_query_iter(sync->world, sync->query);
    while (ecs_query_next(&it))
    {
        for (i32 i = 0; i < it.count; i++)
        {
            void* val = mel_hashmap_get(&sync->entity_map,
                (void*)(usize)it.entities[i]);
            if (val == nullptr) continue;

            u32 entry_index = (u32)((usize)val - 1);
            void* entry = mel_render_list_get(sync->list, entry_index);
            sync->write(entry, &it, i, sync->user);
        }
    }
}
