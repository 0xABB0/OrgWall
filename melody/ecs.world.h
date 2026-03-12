#pragma once

#include "core.types.h"

#include <flecs.h>

typedef struct Mel_ECS {
    ecs_world_t* world;
} Mel_ECS;

void mel_ecs_init(Mel_ECS* ecs);
void mel_ecs_shutdown(Mel_ECS* ecs);
void mel_ecs_update(Mel_ECS* ecs, f32 dt);
