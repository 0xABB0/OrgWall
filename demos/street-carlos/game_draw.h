#pragma once

#include "mugen.fighter.fwd.h"
#include "mugen.char.h"
#include "mugen.camera.h"
#include "mugen.stage.h"
#include "mugen.sff.h"
#include "mugen.cns.h"
#include "texture.pool.fwd.h"
#include "render.list.fwd.h"
#include "math.vec2.h"
#include "math.geo.rect.h"

void game_draw_fighter(Fighter* f, Mugen_Char* mc, Mel_Texture_Handle tex,
    Mugen_Camera* cam, f32 zoffset, f32 scale_x, f32 scale_y, Mel_Render_List* list);

void game_draw_helper(Fighter_Helper* h, Mugen_Char* mc, Mel_Texture_Handle tex,
    Mugen_Camera* cam, f32 zoffset, f32 scale_x, f32 scale_y, Mel_Render_List* list);

void game_draw_debug_boxes(Fighter* f, Mugen_Camera* cam, f32 zoffset,
    f32 scale_x, f32 scale_y, Mel_Render_List* list);

void game_draw_helper_debug_boxes(Fighter_Helper* h, Mugen_Camera* cam, f32 zoffset,
    f32 scale_x, f32 scale_y, Mel_Render_List* list);

void game_draw_afterimage(Mugen_Char_State* st, Mugen_Char* mc, Mel_Texture_Handle tex,
    Mugen_Camera* cam, f32 zoffset, f32 scale_x, f32 scale_y, Mel_Render_List* list);

#include "font.atlas.h"
#include "mugen.command.h"
#include "mugen.match.h"

void game_draw_input_display(Mugen_Player_Inputs inputs, Command_List* cmds,
    i32 stateno, Mel_Font_Atlas_Pool* fonts, Mel_Font_Handle font,
    f32 base_x, f32 base_y, Mel_Render_List* list);

void game_draw_stage_layer(Mugen_Stage* stage, Mugen_Sff* sff,
    Mel_Texture_Handle tex, Mugen_Camera* cam,
    u8 target_layer, Mel_Render_List* list);

void game_draw_char_action_preview(Mugen_Char* mc, Mel_Texture_Handle tex,
    i32 action_no, bool facing_right, Mel_Vec2 foot_pos, f32 scale, Mel_Render_List* list);

bool game_draw_char_portrait_preview(Mugen_Char* mc, Mel_Texture_Handle tex,
    u16 group, u16 number, bool facing_right, Mel_Rect bounds, Mel_Render_List* list);

void game_draw_stage_preview_framed(Mugen_Stage* stage, Mugen_Sff* sff,
    Mel_Texture_Handle tex, Mel_Rect bounds, Mel_Vec2 focus, f32 zoom, Mel_Render_List* list);

void game_draw_stage_preview(Mugen_Stage* stage, Mugen_Sff* sff,
    Mel_Texture_Handle tex, Mel_Rect bounds, Mel_Render_List* list);
