#pragma once

#include "core.types.h"
#include "mugen.fightdef.h"
#include "mugen.sff.h"
#include "font.atlas.fwd.h"
#include "texture.pool.fwd.h"
#include "render.list.fwd.h"

typedef struct {
    f32 p1_life_ratio, p2_life_ratio;
    f32 p1_power_ratio, p2_power_ratio;
    i32 p1_life, p2_life;
    i32 p1_power, p2_power;
    i32 time_count;
    i32 p1_combo, p2_combo;
    i32 p1_wins, p2_wins;
} Mugen_Hud_State;

typedef struct {
    Mugen_Fightdef* fightdef;
    Mugen_Sff* fight_sff;
    Mel_Texture_Handle fight_tex;
    Mel_Font_Atlas_Pool* font_pool;
    Mel_Font_Handle font;
    f32 p1_mid_ratio, p2_mid_ratio;
    f32 p1_power_mid, p2_power_mid;
    f32 scale_x, scale_y;
} Mugen_Hud;

void mugen_hud_draw(Mugen_Hud* hud, Mugen_Hud_State* state, Mel_Render_List* list);
