#pragma once

#include "street_carlos_ctx.h"

void street_carlos_show_title(Street_Carlos_Ctx* ctx);
void street_carlos_show_main_menu(Street_Carlos_Ctx* ctx);
void street_carlos_show_char_select(Street_Carlos_Ctx* ctx);
void street_carlos_show_stage_select(Street_Carlos_Ctx* ctx);
void street_carlos_start_loading(Street_Carlos_Ctx* ctx, Mugen_Char* p1_char, Mugen_Char* p2_char, str8 stage_path);
void street_carlos_start_quick_fight(Street_Carlos_Ctx* ctx);
u32 street_carlos_menu_rand_index(Street_Carlos_Ctx* ctx, u32 count);
