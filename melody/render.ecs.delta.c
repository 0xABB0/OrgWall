#include "render.ecs.delta.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <flecs.h>
#include <string.h>

static void mel__delta_list_push(Mel_ECS_Delta_List* list, ecs_entity_t entity, const Mel_Alloc* alloc)
{
    if (list->count >= list->capacity)
    {
        u32 new_cap = list->capacity == 0 ? 64 : list->capacity * 2;
        ecs_entity_t* new_buf = (ecs_entity_t*)mel_alloc(alloc, new_cap * sizeof(ecs_entity_t));
        if (list->entities)
        {
            memcpy(new_buf, list->entities, list->count * sizeof(ecs_entity_t));
            mel_dealloc(alloc, list->entities);
        }
        list->entities = new_buf;
        list->capacity = new_cap;
    }
    list->entities[list->count++] = entity;
}

static bool mel__delta_list_contains(const Mel_ECS_Delta_List* list, ecs_entity_t entity)
{
    for (u32 i = 0; i < list->count; i++)
    {
        if (list->entities[i] == entity)
            return true;
    }
    return false;
}

static void mel__delta_list_free(Mel_ECS_Delta_List* list, const Mel_Alloc* alloc)
{
    if (list->entities)
        mel_dealloc(alloc, list->entities);
    *list = (Mel_ECS_Delta_List){0};
}

static void mel__on_add_callback(ecs_iter_t* it)
{
    Mel_ECS_Delta* delta = it->ctx;
    for (i32 i = 0; i < it->count; i++)
        mel__delta_list_push(&delta->added, it->entities[i], delta->alloc);
}

static void mel__on_remove_callback(ecs_iter_t* it)
{
    Mel_ECS_Delta* delta = it->ctx;
    for (i32 i = 0; i < it->count; i++)
        mel__delta_list_push(&delta->removed, it->entities[i], delta->alloc);
}

static void mel__on_set_callback(ecs_iter_t* it)
{
    Mel_ECS_Delta* delta = it->ctx;
    for (i32 i = 0; i < it->count; i++)
    {
        ecs_entity_t e = it->entities[i];
        if (!mel__delta_list_contains(&delta->added, e) &&
            !mel__delta_list_contains(&delta->modified, e))
        {
            mel__delta_list_push(&delta->modified, e, delta->alloc);
        }
    }
}

void mel_ecs_delta_init_opt(Mel_ECS_Delta* delta, Mel_ECS_Delta_Opt opt)
{
    assert(delta != nullptr);
    assert(opt.world != nullptr);
    assert(opt.components[0] != 0);

    *delta = (Mel_ECS_Delta){0};
    delta->world = opt.world;
    delta->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    i32 term_count = 0;
    while (term_count < 8 && opt.components[term_count] != 0)
        term_count++;

    ecs_observer_desc_t add_desc = {0};
    for (i32 i = 0; i < term_count; i++)
        add_desc.query.terms[i].id = opt.components[i];
    add_desc.events[0] = EcsOnAdd;
    add_desc.callback = mel__on_add_callback;
    add_desc.ctx = delta;

    delta->on_add_observer = ecs_observer_init(opt.world, &add_desc);
    assert(delta->on_add_observer != 0);

    ecs_observer_desc_t remove_desc = {0};
    for (i32 i = 0; i < term_count; i++)
        remove_desc.query.terms[i].id = opt.components[i];
    remove_desc.events[0] = EcsOnRemove;
    remove_desc.callback = mel__on_remove_callback;
    remove_desc.ctx = delta;

    delta->on_remove_observer = ecs_observer_init(opt.world, &remove_desc);
    assert(delta->on_remove_observer != 0);

    ecs_observer_desc_t set_desc = {0};
    for (i32 i = 0; i < term_count; i++)
        set_desc.query.terms[i].id = opt.components[i];
    set_desc.events[0] = EcsOnSet;
    set_desc.callback = mel__on_set_callback;
    set_desc.ctx = delta;

    delta->on_set_observer = ecs_observer_init(opt.world, &set_desc);
    assert(delta->on_set_observer != 0);
}

void mel_ecs_delta_shutdown(Mel_ECS_Delta* delta)
{
    assert(delta != nullptr);

    if (delta->world)
    {
        ecs_delete(delta->world, delta->on_add_observer);
        ecs_delete(delta->world, delta->on_remove_observer);
        ecs_delete(delta->world, delta->on_set_observer);
    }

    mel__delta_list_free(&delta->added, delta->alloc);
    mel__delta_list_free(&delta->removed, delta->alloc);
    mel__delta_list_free(&delta->modified, delta->alloc);

    *delta = (Mel_ECS_Delta){0};
}

void mel_ecs_delta_begin_frame(Mel_ECS_Delta* delta)
{
    assert(delta != nullptr);
    delta->added.count = 0;
    delta->removed.count = 0;
    delta->modified.count = 0;
}

u32 mel_ecs_delta_added_count(const Mel_ECS_Delta* delta)
{
    return delta->added.count;
}

u32 mel_ecs_delta_removed_count(const Mel_ECS_Delta* delta)
{
    return delta->removed.count;
}

u32 mel_ecs_delta_modified_count(const Mel_ECS_Delta* delta)
{
    return delta->modified.count;
}

const ecs_entity_t* mel_ecs_delta_added(const Mel_ECS_Delta* delta)
{
    return delta->added.entities;
}

const ecs_entity_t* mel_ecs_delta_removed(const Mel_ECS_Delta* delta)
{
    return delta->removed.entities;
}

const ecs_entity_t* mel_ecs_delta_modified(const Mel_ECS_Delta* delta)
{
    return delta->modified.entities;
}
