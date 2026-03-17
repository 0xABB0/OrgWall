#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "ecs.world.fwd.h"

typedef struct Mel_ECS_Delta Mel_ECS_Delta;

typedef struct Mel_ECS_Delta_List Mel_ECS_Delta_List;
struct Mel_ECS_Delta_List
{
    ecs_entity_t* entities;
    u32 count;
    u32 capacity;
};

struct Mel_ECS_Delta
{
    ecs_world_t* world;
    Mel_ECS_Delta_List added;
    Mel_ECS_Delta_List removed;
    Mel_ECS_Delta_List modified;
    ecs_entity_t on_add_observer;
    ecs_entity_t on_remove_observer;
    ecs_entity_t on_set_observer;
    const Mel_Alloc* alloc;
};

typedef struct {
    ecs_world_t* world;
    ecs_id_t components[8];
    const Mel_Alloc* alloc;
} Mel_ECS_Delta_Opt;

void mel_ecs_delta_init_opt(Mel_ECS_Delta* delta, Mel_ECS_Delta_Opt opt);
#define mel_ecs_delta_init(delta, ...) mel_ecs_delta_init_opt((delta), (Mel_ECS_Delta_Opt){__VA_ARGS__})

void mel_ecs_delta_shutdown(Mel_ECS_Delta* delta);

void mel_ecs_delta_begin_frame(Mel_ECS_Delta* delta);

u32 mel_ecs_delta_added_count(const Mel_ECS_Delta* delta);
u32 mel_ecs_delta_removed_count(const Mel_ECS_Delta* delta);
u32 mel_ecs_delta_modified_count(const Mel_ECS_Delta* delta);

const ecs_entity_t* mel_ecs_delta_added(const Mel_ECS_Delta* delta);
const ecs_entity_t* mel_ecs_delta_removed(const Mel_ECS_Delta* delta);
const ecs_entity_t* mel_ecs_delta_modified(const Mel_ECS_Delta* delta);
