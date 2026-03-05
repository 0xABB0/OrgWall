#pragma once

#include "render.sync.fwd.h"
#include "core.types.h"
#include "collection.hashmap.h"
#include "render.list.fwd.h"
#include "allocator.fwd.h"

#include <flecs.h>

typedef void (*Mel_Render_Sync_Write_Fn)(void* entry, ecs_iter_t* it, i32 row, void* user);
typedef u64  (*Mel_Render_Sync_Key_Fn)(ecs_iter_t* it, i32 row, void* user);

struct Mel_Render_Sync {
    Mel_Render_List* list;
    ecs_world_t* world;
    Mel_HashMap entity_map;
    ecs_entity_t on_set;
    ecs_entity_t on_remove;
    ecs_query_t* query;
    Mel_Render_Sync_Write_Fn write;
    Mel_Render_Sync_Key_Fn key;
    void* user;
};

typedef struct {
    Mel_Render_List* list;
    ecs_world_t* world;
    ecs_id_t components[8];
    Mel_Render_Sync_Write_Fn write;
    Mel_Render_Sync_Key_Fn key;
    void* user;
    const Mel_Alloc* alloc;
} Mel_Render_Sync_Opt;

void mel_render_sync_init_opt(Mel_Render_Sync* sync, Mel_Render_Sync_Opt opt);
#define mel_render_sync_init(sync, ...) mel_render_sync_init_opt((sync), (Mel_Render_Sync_Opt){__VA_ARGS__})

void mel_render_sync_shutdown(Mel_Render_Sync* sync);
void mel_render_sync_update(Mel_Render_Sync* sync);
