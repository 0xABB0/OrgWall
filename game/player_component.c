#include "player.h"

ECS_COMPONENT_DECLARE(Mel_CPlayer);

void mel_component_player_register(ecs_world_t* world)
{
    ECS_COMPONENT_DEFINE(world, Mel_CPlayer);
}
