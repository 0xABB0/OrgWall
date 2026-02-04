#ifndef MEL_CPLAYER_H
#define MEL_CPLAYER_H

#include "types.h"

#include <flecs.h>

typedef struct
{
    f32 speed;
    bool input_up;
    bool input_down;
    bool input_left;
    bool input_right;
} Mel_CPlayer;

extern ECS_COMPONENT_DECLARE(Mel_CPlayer);
void mel_component_player_register(ecs_world_t* world);
bool mel_ed_player_draw(ecs_world_t* world, ecs_entity_t e);

#endif
