#ifndef MEL_CNPC_H
#define MEL_CNPC_H

#include "types.h"

#include <flecs.h>

typedef struct
{
    const char* dialogue;
    f32 interact_radius;
} Mel_CNPC;

extern ECS_COMPONENT_DECLARE(Mel_CNPC);
void mel_component_npc_register(ecs_world_t* world);
bool mel_ed_npc_draw(ecs_world_t* world, ecs_entity_t e);

#endif
