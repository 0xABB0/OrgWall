#ifndef MEL_ED_ENTITIES_H
#define MEL_ED_ENTITIES_H

#include "types.h"
#include "allocator.fwd.h"
#include "array.h"
#include "ecs.world.h"

typedef bool (*Mel_ComponentInspector_Fn)(ecs_world_t* world, ecs_entity_t e);

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

    Mel_Array(Mel_ComponentInspector_Fn) inspectors;
} Mel_EdEntities;

void mel_ed_entities_init(Mel_EdEntities* ed, const Mel_Alloc* alloc);
void mel_ed_entities_shutdown(Mel_EdEntities* ed);

void mel_ed_entities_set_world(Mel_EdEntities* ed, ecs_world_t* world);
void mel_ed_entities_draw(Mel_EdEntities* ed, f32 dt);
void mel_ed_entities_register_inspector(Mel_EdEntities* ed, Mel_ComponentInspector_Fn fn);

#endif
