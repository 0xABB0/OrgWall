#include "collider.h"

ECS_COMPONENT_DECLARE(Mel_CCollider);

void mel_component_collider_register(ecs_world_t* world)
{
    ECS_COMPONENT_DEFINE(world, Mel_CCollider);
}
