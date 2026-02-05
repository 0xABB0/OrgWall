#ifndef MEL_CCOLLIDER_H
#define MEL_CCOLLIDER_H

#include "types.h"
#include "math.vec2.h"

#include <flecs.h>

typedef struct
{
    Mel_Vec2 size;
} Mel_CCollider;

extern ECS_COMPONENT_DECLARE(Mel_CCollider);
void mel_component_collider_register(ecs_world_t* world);
bool mel_ed_collider_draw(ecs_world_t* world, ecs_entity_t e);

#endif
