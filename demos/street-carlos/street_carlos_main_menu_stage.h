#pragma once

#include "street_carlos_ctx.h"

typedef enum {
    STREET_CARLOS_MAIN_MENU_1V1 = 0,
    STREET_CARLOS_MAIN_MENU_QUICK_FIGHT = 1,
} Street_Carlos_Main_Menu_Item;

struct Street_Carlos_Main_Menu_Stage {
    Mel_Stage stage;
    u32 selected_index;
};

void street_carlos_main_menu_stage_init(Street_Carlos_Main_Menu_Stage* stage, Street_Carlos_Ctx* ctx);
void street_carlos_main_menu_stage_shutdown(Street_Carlos_Main_Menu_Stage* stage);
bool street_carlos_main_menu_stage_handle_event(Street_Carlos_Main_Menu_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event);
void street_carlos_main_menu_stage_draw_world(Street_Carlos_Main_Menu_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list);

