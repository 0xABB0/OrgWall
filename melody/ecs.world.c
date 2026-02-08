#include "ecs.world.h"
#include <tracy/TracyC.h>

void mel_ecs_init(Mel_ECS* ecs)
{
    assert(ecs != nullptr);
    *ecs = (Mel_ECS){0};
    ecs->world = ecs_init();
}

void mel_ecs_shutdown(Mel_ECS* ecs)
{
    assert(ecs != nullptr);

    if (ecs->world)
    {
        ecs_fini(ecs->world);
        ecs->world = nullptr;
    }
}

void mel_ecs_update(Mel_ECS* ecs, f32 dt)
{
    TracyCZoneN(ctx, "ecs_progress", true);
    assert(ecs != nullptr);
    ecs_progress(ecs->world, dt);
    TracyCZoneEnd(ctx);
}
