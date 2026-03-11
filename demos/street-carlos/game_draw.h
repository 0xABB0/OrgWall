#pragma once

#include "mugen.fighter.fwd.h"
#include "mugen.char.h"
#include "mugen.camera.h"
#include "mugen.stage.h"
#include "mugen.sff.h"
#include "texture.pool.fwd.h"
#include "render.list.fwd.h"

void game_draw_fighter(Fighter* f, Mugen_Char* mc, Mel_Texture_Handle tex,
    Mugen_Camera* cam, f32 zoffset, f32 scale_x, f32 scale_y, Mel_Render_List* list);

void game_draw_helper(Fighter_Helper* h, Mugen_Char* mc, Mel_Texture_Handle tex,
    Mugen_Camera* cam, f32 zoffset, f32 scale_x, f32 scale_y, Mel_Render_List* list);

void game_draw_debug_boxes(Fighter* f, Mugen_Camera* cam, f32 zoffset,
    f32 scale_x, f32 scale_y, Mel_Render_List* list);

void game_draw_helper_debug_boxes(Fighter_Helper* h, Mugen_Camera* cam, f32 zoffset,
    f32 scale_x, f32 scale_y, Mel_Render_List* list);

void game_draw_stage_layer(Mugen_Stage* stage, Mugen_Sff* sff,
    Mel_Texture_Handle tex, Mugen_Camera* cam,
    u8 target_layer, Mel_Render_List* list);
