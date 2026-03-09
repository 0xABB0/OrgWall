#pragma once

#include "fighter.h"
#include "mugen_char.h"
#include "render.list.fwd.h"

void game_draw_fighter(Fighter* f, Mugen_Char* mc, Mel_Render_List* list);

void game_draw_helper(Fighter_Helper* h, Mugen_Char* mc, Mel_Render_List* list);

void game_draw_debug_boxes(Fighter* f, Mel_Render_List* list);

void game_draw_helper_debug_boxes(Fighter_Helper* h, Mel_Render_List* list);

void game_draw_stage(Mel_Render_List* list);
