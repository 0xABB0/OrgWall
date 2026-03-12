#include "street_carlos_main_menu_stage.h"

#include "street_carlos_flow.h"
#include "math.vec2.h"
#include "math.vec4.h"

static void draw_centered_text(Street_Carlos_Ctx* ctx, Mel_Render_List* list, Mel_Font_Handle font, str8 text, f32 y, Mel_Vec4 color)
{
    Mel_Vec2 size = mel_font_atlas_measure_text(&ctx->font_pool, font, text);
    f32 x = (f32)GAME_W * 0.5f - size.x * 0.5f;
    mel_font_atlas_draw_text(&ctx->font_pool, font, list, text, x, y, color);
}

static void street_carlos_main_menu_stage_start(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    Street_Carlos_Main_Menu_Stage* stage = user;
    stage->selected_index = STREET_CARLOS_MAIN_MENU_1V1;
}

void street_carlos_main_menu_stage_init(Street_Carlos_Main_Menu_Stage* stage, Street_Carlos_Ctx* ctx)
{
    mel_stage_init(&stage->stage,
        .on_start = street_carlos_main_menu_stage_start,
        .user = stage,
        .start_enabled = false);
    MEL_UNUSED(ctx);
}

void street_carlos_main_menu_stage_shutdown(Street_Carlos_Main_Menu_Stage* stage)
{
    mel_stage_shutdown(&stage->stage);
}

bool street_carlos_main_menu_stage_handle_event(Street_Carlos_Main_Menu_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event)
{
    if (!mel_stage_is_enabled(&stage->stage) || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat)
        return false;

    const u32 item_count = 2;
    if (event->key.scancode == SDL_SCANCODE_UP)
    {
        stage->selected_index = (stage->selected_index + item_count - 1) % item_count;
        return true;
    }
    if (event->key.scancode == SDL_SCANCODE_DOWN)
    {
        stage->selected_index = (stage->selected_index + 1) % item_count;
        return true;
    }
    if (event->key.scancode != SDL_SCANCODE_RETURN)
        return false;

    if (stage->selected_index == STREET_CARLOS_MAIN_MENU_1V1)
        street_carlos_show_char_select(ctx);
    else
        street_carlos_start_quick_fight(ctx);
    return true;
}

void street_carlos_main_menu_stage_draw_world(Street_Carlos_Main_Menu_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list)
{
    if (!mel_stage_is_enabled(&stage->stage))
        return;

    draw_centered_text(ctx, list, ctx->title_font, S8("MAIN MENU"), 48.0f, mel_vec4(1.0f, 0.3f, 0.3f, 1.0f));

    str8 options[] = { S8("1V1"), S8("Quick Fight") };
    for (u32 i = 0; i < 2; i++)
    {
        Mel_Vec4 color = i == stage->selected_index
            ? mel_vec4(1.0f, 1.0f, 0.4f, 1.0f)
            : mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
        draw_centered_text(ctx, list, ctx->ui_font, options[i], 98.0f + (f32)i * 26.0f, color);
    }

    draw_centered_text(ctx, list, ctx->ui_font, S8("UP/DOWN to choose"), 176.0f,
        mel_vec4(0.8f, 0.8f, 0.8f, 1.0f));
    draw_centered_text(ctx, list, ctx->ui_font, S8("ENTER to confirm"), 192.0f,
        mel_vec4(0.8f, 0.8f, 0.8f, 1.0f));
}
