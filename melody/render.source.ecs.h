#pragma once

#include "render.source.type.h"
#include "render.manager.h"
#include "ecs.world.fwd.h"
#include "allocator.fwd.h"

extern const Mel_Render_Source_Type mel_source_ecs_type;

typedef void (*Mel_ECS_Source_On_Add)(Mel_Render_Source* source,
                                      Mel_Render_Manager* mgr,
                                      ecs_world_t* world,
                                      ecs_entity_t entity,
                                      Mel_Render_Handle h);

typedef void (*Mel_ECS_Source_On_Modify)(Mel_Render_Source* source,
                                          Mel_Render_Manager* mgr,
                                          ecs_world_t* world,
                                          ecs_entity_t entity,
                                          Mel_Render_Handle h);

typedef struct {
    ecs_world_t* world;
    ecs_id_t components[8];
    Mel_ECS_Source_On_Add on_add;
    Mel_ECS_Source_On_Modify on_modify;
    const Mel_Alloc* alloc;
} Mel_Source_ECS_Opt;

Mel_Render_Source* mel_source_ecs_create_opt(Mel_Source_ECS_Opt opt);
#define mel_source_ecs_create(...) mel_source_ecs_create_opt((Mel_Source_ECS_Opt){__VA_ARGS__})

Mel_Render_Handle mel_source_ecs_handle_for_entity(Mel_Render_Source* source, ecs_entity_t entity);
