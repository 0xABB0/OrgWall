#include "npc.h"

ECS_COMPONENT_DECLARE(Mel_CNPC);

void mel_component_npc_register(ecs_world_t* world)
{
    ECS_COMPONENT_DEFINE(world, Mel_CNPC);
}
