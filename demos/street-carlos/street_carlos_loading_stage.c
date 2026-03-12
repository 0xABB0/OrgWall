#include "street_carlos_loading_stage.h"

#include "street_carlos_fight_stage.h"

#include "render.list.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "sprite.pass.h"

static void draw_centered_text(Street_Carlos_Ctx* ctx, Mel_Render_List* list, Mel_Font_Handle font, str8 text, f32 y, Mel_Vec4 color)
{
    Mel_Vec2 size = mel_font_atlas_measure_text(&ctx->font_pool, font, text);
    f32 x = (f32)GAME_W * 0.5f - size.x * 0.5f;
    mel_font_atlas_draw_text(&ctx->font_pool, font, list, text, x, y, color);
}

static void street_carlos_loading_stage_start(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    MEL_UNUSED(user);
}

void street_carlos_loading_stage_init(Street_Carlos_Loading_Stage* stage, Street_Carlos_Ctx* ctx)
{
    mel_loading_stage_init(&stage->base,
        .progress = &ctx->load_progress,
        .next = &ctx->fight_stage->stage,
        .ready_at = 1.0f,
        .attach_next = true,
        .enable_next = true,
        .detach_self = true);
    stage->base.stage.on_start = street_carlos_loading_stage_start;
}

void street_carlos_loading_stage_shutdown(Street_Carlos_Loading_Stage* stage)
{
    mel_loading_stage_shutdown(&stage->base);
}

bool street_carlos_loading_stage_handle_event(Street_Carlos_Loading_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event)
{
    MEL_UNUSED(ctx);
    MEL_UNUSED(event);
    return mel_stage_is_enabled(&stage->base.stage);
}

void street_carlos_loading_stage_draw_world(Street_Carlos_Loading_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list)
{
    if (!mel_stage_is_enabled(&stage->base.stage))
        return;

    draw_centered_text(ctx, list, ctx->ui_font, S8("Loading..."), 92.0f, mel_vec4(1.0f, 1.0f, 1.0f, 1.0f));

    f32 progress = mel_progress_value(&ctx->load_progress);
    f32 bar_w = 200.0f;
    f32 bar_h = 8.0f;
    f32 bar_x = (f32)GAME_W * 0.5f - bar_w * 0.5f;
    f32 bar_y = 120.0f;

    mel_draw_sprite(list, .pos = mel_vec2(bar_x, bar_y),
        .size = mel_vec2(bar_w, bar_h),
        .color = mel_vec4(0.2f, 0.2f, 0.2f, 1.0f));

    mel_draw_sprite(list, .pos = mel_vec2(bar_x, bar_y),
        .size = mel_vec2(bar_w * progress, bar_h),
        .color = mel_vec4(1.0f, 0.3f, 0.3f, 1.0f));
}
