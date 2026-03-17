#include "ecs.2d.text.h"
#include "ecs.2d.transform.h"

ECS_COMPONENT_DECLARE(Mel_CText);

void mel_component_text_register(ecs_world_t* world)
{
    ECS_COMPONENT_DEFINE(world, Mel_CText);
}

void mel_text_system_run_opt(ecs_world_t* world, Mel_Text_System_Opt opt)
{
    (void)world;
    (void)opt;
}
