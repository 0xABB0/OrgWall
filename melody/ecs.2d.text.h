#pragma once

#include "core.types.h"
#include "math.vec4.h"
#include "string.str8.fwd.h"
#include "font.atlas.fwd.h"
#include "texture.pool.fwd.h"
#include "render.list.fwd.h"

#include <flecs.h>

typedef struct Mel_CText {
    str8 text;
    Mel_Font_Handle font;
    Mel_Vec4 color;
} Mel_CText;

extern ECS_COMPONENT_DECLARE(Mel_CText);
void mel_component_text_register(ecs_world_t* world);

typedef struct {
    Mel_Render_List* list;
    Mel_Font_Atlas_Pool* font_pool;
} Mel_Text_System_Opt;

void mel_text_system_run_opt(ecs_world_t* world, Mel_Text_System_Opt opt);
#define mel_text_system_run(world, ...) mel_text_system_run_opt((world), (Mel_Text_System_Opt){__VA_ARGS__})
