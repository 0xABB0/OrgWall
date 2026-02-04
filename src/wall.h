#ifndef MEL_WALL_H
#define MEL_WALL_H

#include <flecs.h>

extern ECS_TAG_DECLARE(Wall);
void mel_component_wall_register(ecs_world_t* world);

#endif
