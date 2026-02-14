#pragma once

#include "core.types.h"
#include "math.vec2.h"

#include <flecs.h>

typedef struct
{
    Mel_Vec2 pos;
    Mel_Vec2 vel;
} Mel_CTransform;

extern ECS_COMPONENT_DECLARE(Mel_CTransform);
void mel_component_transform_register(ecs_world_t* world);
bool mel_ed_transform_draw(ecs_world_t* world, ecs_entity_t e);
