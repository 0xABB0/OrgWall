#pragma once

#include "render.source.type.h"
#include "render.manager.fwd.h"
#include "render.types.2d.h"
#include "ecs.world.fwd.h"
#include "allocator.fwd.h"

extern const Mel_Render_Source_Type mel_source_ecs_2d_type;

typedef struct {
    ecs_world_t* world;
    const Mel_Alloc* alloc;
} Mel_Source_ECS_2D_Opt;

Mel_Render_Source* mel_source_ecs_2d_create_opt(Mel_Source_ECS_2D_Opt opt);
#define mel_source_ecs_2d_create(...) mel_source_ecs_2d_create_opt((Mel_Source_ECS_2D_Opt){__VA_ARGS__})

Mel_Render_Handle mel_source_ecs_2d_handle_for_entity(Mel_Render_Source* source, ecs_entity_t entity);
bool mel_source_ecs_2d_get_sprite_payload(Mel_Render_Source* source,
                                          Mel_Render_Handle h,
                                          Mel_Render_Transform_2D* transform,
                                          Mel_Render_Sprite_Info* info);
