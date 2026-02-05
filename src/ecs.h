#ifndef MEL_ECS_H
#define MEL_ECS_H

#include "types.h"
#include "math.vec2.h"

#include <flecs.h>

#include "transform.h"
#include "sprite.h"
#include "player.h"
#include "npc.h"
#include "collider.h"
#include "wall.h"

typedef struct
{
    ecs_world_t* world;
    ecs_entity_t player;
} Mel_ECS;

void mel_ecs_init(Mel_ECS* ecs);
void mel_ecs_shutdown(Mel_ECS* ecs);
void mel_ecs_update(Mel_ECS* ecs, f32 dt);

ecs_entity_t mel_ecs_create_player(Mel_ECS* ecs, Mel_Vec2 pos, f32 speed);
ecs_entity_t mel_ecs_create_npc(Mel_ECS* ecs, Mel_Vec2 pos, const char* dialogue);
ecs_entity_t mel_ecs_create_wall(Mel_ECS* ecs, Mel_Vec2 pos, Mel_Vec2 size);

#endif
