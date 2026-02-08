#ifndef MEL_ECS_2D_SPRITE_H
#define MEL_ECS_2D_SPRITE_H

#include "types.h"
#include "math.vec2.h"
#include "math.vec4.h"

#include <flecs.h>

typedef struct Mel_Sprite
{
    Mel_Vec2 size;
    Mel_Vec4 color;
} Mel_Sprite;

extern ECS_COMPONENT_DECLARE(Mel_Sprite);
void mel_component_sprite_register(ecs_world_t* world);
bool mel_editor_sprite(ecs_world_t* world, ecs_entity_t e);

#endif
