#ifndef MEL_SPRITE_H
#define MEL_SPRITE_H

#include "types.h"
#include "vec2.h"
#include "vec4.h"

#include <flecs.h>

typedef struct Mel_Sprite
{
    Mel_Vec2 size;
    Mel_Vec4 color;
} Mel_Sprite;

extern ECS_COMPONENT_DECLARE(Mel_Sprite);

void mel_sprite_init();
void mel_sprite_deinit();

void mel_component_sprite_register(ecs_world_t* world);

bool mel_editor_sprite(ecs_world_t* world, ecs_entity_t e);

#endif
