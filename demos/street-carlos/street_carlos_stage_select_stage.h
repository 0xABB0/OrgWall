#pragma once

#include "street_carlos_ctx.h"
#include "mugen.stage.h"
#include "mugen.sff.h"
#include "gpu.texture.h"
#include "texture.pool.h"
#include "math.vec2.h"

typedef struct {
    Mugen_Stage stage;
    Mugen_Sff sff;
    Mel_Gpu_Texture texture;
    Mel_Texture_Handle handle;
    Mel_Vec2 preview_focus;
    f32 preview_zoom;
    bool loaded;
} Street_Carlos_Stage_Preview;

struct Street_Carlos_Stage_Select_Stage {
    Mel_Stage stage;
    Street_Carlos_Stage_Preview* previews;
    u32 preview_count;
    u32 cursor;
};

void street_carlos_stage_select_stage_init(Street_Carlos_Stage_Select_Stage* stage, Street_Carlos_Ctx* ctx);
void street_carlos_stage_select_stage_shutdown(Street_Carlos_Stage_Select_Stage* stage);
bool street_carlos_stage_select_stage_handle_event(Street_Carlos_Stage_Select_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event);
void street_carlos_stage_select_stage_draw_world(Street_Carlos_Stage_Select_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list);
