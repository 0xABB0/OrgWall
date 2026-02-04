#include "wall.h"

ECS_TAG_DECLARE(Wall);

void mel_component_wall_register(ecs_world_t* world)
{
    ECS_TAG_DEFINE(world, Wall);
}
