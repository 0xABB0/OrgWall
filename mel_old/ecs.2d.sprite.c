#include "ecs.2d.sprite.h"

ECS_COMPONENT_DECLARE(Mel_Sprite);

void mel_component_sprite_register(ecs_world_t* world)
{
    ECS_COMPONENT_DEFINE(world, Mel_Sprite);
}
