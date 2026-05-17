#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "str8.fwd.h"
#include "collection.array.fwd.h"

#include <SDL3/SDL_events.h>

typedef void (*Mel_EdDraw_Fn)(void* instance, f32 dt);
typedef void (*Mel_EdShutdown_Fn)(void* instance);
typedef void (*Mel_EdEvent_Fn)(void* instance, SDL_Event* event);

typedef struct Mel_EdEntry
{
    str8               name;
    void*              data;
    u32                id;
    bool               open;
    char               window_title[128];
    Mel_EdDraw_Fn      draw;
    Mel_EdShutdown_Fn  shutdown;
    Mel_EdEvent_Fn     event;
} Mel_EdEntry;

typedef struct Mel_EdRegistry
{
    Mel_Array(Mel_EdEntry) entries;
    u32 next_id;
} Mel_EdRegistry;

typedef struct
{
    const Mel_Alloc* alloc;
} Mel_EdRegistry_Init_Opt;

void mel_ed_registry_init_opt(Mel_EdRegistry* reg, Mel_EdRegistry_Init_Opt opt);
#define mel_ed_registry_init(reg, ...) mel_ed_registry_init_opt((reg), (Mel_EdRegistry_Init_Opt){__VA_ARGS__})

void mel_ed_registry_shutdown(Mel_EdRegistry* reg);

typedef struct
{
    str8               name;
    void*              data;
    Mel_EdDraw_Fn      draw;
    Mel_EdShutdown_Fn  shutdown;
    Mel_EdEvent_Fn     event;
} Mel_EdRegistry_Add_Opt;

Mel_EdEntry* mel_ed_registry_add_opt(Mel_EdRegistry* reg, Mel_EdRegistry_Add_Opt opt);
#define mel_ed_registry_add(reg, ...) mel_ed_registry_add_opt((reg), (Mel_EdRegistry_Add_Opt){__VA_ARGS__})

void mel_ed_registry_remove(Mel_EdRegistry* reg, Mel_EdEntry* entry);
void mel_ed_registry_draw(Mel_EdRegistry* reg, f32 dt);
void mel_ed_registry_process_event(Mel_EdRegistry* reg, SDL_Event* event);
usize mel_ed_registry_count(Mel_EdRegistry* reg);
Mel_EdEntry* mel_ed_registry_at(Mel_EdRegistry* reg, usize idx);
