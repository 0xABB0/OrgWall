#include "street_carlos_title_stage.h"

#include "street_carlos_flow.h"
#include "math.vec2.h"
#include "math.vec4.h"

static void draw_centered_text(Street_Carlos_Ctx* ctx, Mel_Render_List* list, Mel_Font_Atlas_Handle font, str8 text, f32 y, Mel_Vec4 color)
{
    Mel_Vec2 size = mel_font_atlas_measure_text(font, text);
    f32 x = (f32)GAME_W * 0.5f - size.x * 0.5f;
    mel_font_atlas_draw_text(font, list, text, x, y, color);
}

static void street_carlos_title_stage_start(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    MEL_UNUSED(user);
    SDL_Log("Street Carlos - Title Screen");
}

void street_carlos_title_stage_init(Street_Carlos_Title_Stage* stage, Street_Carlos_Ctx* ctx)
{
    mel_stage_init(&stage->stage,
        .on_start = street_carlos_title_stage_start,
        .user = ctx,
        .start_enabled = false);
}

void street_carlos_title_stage_shutdown(Street_Carlos_Title_Stage* stage)
{
    mel_stage_shutdown(&stage->stage);
}

bool street_carlos_title_stage_handle_event(Street_Carlos_Title_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event)
{
    if (!mel_stage_is_enabled(&stage->stage))
        return false;

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_RETURN && !event->key.repeat)
    {
        street_carlos_show_main_menu(ctx);
        return true;
    }

    return false;
}

void street_carlos_title_stage_draw_world(Street_Carlos_Title_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list)
{
    if (!mel_stage_is_enabled(&stage->stage))
        return;

    draw_centered_text(ctx, list, ctx->title_font, S8("STREET CARLOS"), 60.0f, mel_vec4(1.0f, 0.3f, 0.3f, 1.0f));

    f32 t = (f32)SDL_GetTicks() / 1000.0f;
    f32 alpha = 0.5f + 0.5f * SDL_sinf(t * 3.0f);
    draw_centered_text(ctx, list, ctx->ui_font, S8("Press ENTER to start"), 112.0f, mel_vec4(1.0f, 1.0f, 1.0f, alpha));
}
