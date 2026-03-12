#pragma once

#include "street_carlos_ctx.h"

struct Street_Carlos_Title_Stage {
    Mel_Stage stage;
};

void street_carlos_title_stage_init(Street_Carlos_Title_Stage* stage, Street_Carlos_Ctx* ctx);
void street_carlos_title_stage_shutdown(Street_Carlos_Title_Stage* stage);
bool street_carlos_title_stage_handle_event(Street_Carlos_Title_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event);
void street_carlos_title_stage_draw_world(Street_Carlos_Title_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list);

