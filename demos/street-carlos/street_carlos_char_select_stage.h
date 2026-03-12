#pragma once

#include "street_carlos_ctx.h"
#include "gpu.texture.h"
#include "texture.pool.h"

typedef struct {
    Mel_Gpu_Texture texture;
    Mel_Texture_Handle handle;
} Street_Carlos_Char_Preview;

struct Street_Carlos_Char_Select_Stage {
    Mel_Stage stage;
    Street_Carlos_Char_Preview* previews;
    u32 preview_count;
    u32 p1_cursor;
    u32 p2_cursor;
    bool p1_locked;
    bool p2_locked;
};

void street_carlos_char_select_stage_init(Street_Carlos_Char_Select_Stage* stage, Street_Carlos_Ctx* ctx);
void street_carlos_char_select_stage_shutdown(Street_Carlos_Char_Select_Stage* stage, Street_Carlos_Ctx* ctx);
bool street_carlos_char_select_stage_handle_event(Street_Carlos_Char_Select_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event);
void street_carlos_char_select_stage_draw_world(Street_Carlos_Char_Select_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list);

