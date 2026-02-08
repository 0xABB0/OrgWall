#ifndef MEL_ED_ENTITIES_H
#define MEL_ED_ENTITIES_H

#include "types.h"
#include "ecs.h"

typedef struct
{
    ecs_world_t* world;
    ecs_entity_t selected_entity;
    char query_buffer[512];
    char entity_name_buffer[128];
    ecs_query_t* active_query;
    bool show_builtin;
    bool show_disabled;
    f32 refresh_timer;
    f32 refresh_interval;

    i32 entity_count;
    i32 table_count;
    i32 component_count;
    i32 system_count;
} Mel_EdEntities;

void mel_ed_entities_init(Mel_EdEntities* ed);
void mel_ed_entities_shutdown(Mel_EdEntities* ed);

void mel_ed_entities_set_world(Mel_EdEntities* ed, ecs_world_t* world);
void mel_ed_entities_draw(Mel_EdEntities* ed, f32 dt);

#endif
