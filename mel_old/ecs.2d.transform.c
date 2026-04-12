#include "ecs.2d.transform.h"

ECS_COMPONENT_DECLARE(Mel_CTransform);

void mel_component_transform_register(ecs_world_t* world)
{
    ECS_COMPONENT_DEFINE(world, Mel_CTransform);
}
