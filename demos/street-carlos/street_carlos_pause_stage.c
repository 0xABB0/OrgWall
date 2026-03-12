#include "street_carlos_pause_stage.h"

#include "allocator.heap.h"
#include "street_carlos_flow.h"
#include "street_carlos_fight_stage.h"
#include "math.vec2.h"
#include "math.vec4.h"

static str8 s_pause_labels[STREET_CARLOS_PAUSE_ITEM_COUNT] = {
    S8("Resume"),
    S8("Character Select"),
    S8("Main Menu"),
    S8("Title Screen"),
};

static void street_carlos_pause_stage_sync_buttons(Street_Carlos_Pause_Stage* stage)
{
    for (u32 i = 0; i < STREET_CARLOS_PAUSE_ITEM_COUNT; i++)
    {
        bool selected = i == stage->selected_index;
        stage->buttons[i].normal_color = selected
            ? mel_vec4(0.45f, 0.16f, 0.16f, 0.98f)
            : mel_vec4(0.12f, 0.12f, 0.16f, 0.96f);
        stage->buttons[i].hover_color = selected
            ? mel_vec4(0.55f, 0.20f, 0.20f, 1.0f)
            : mel_vec4(0.20f, 0.20f, 0.26f, 1.0f);
        stage->buttons[i].pressed_color = mel_vec4(0.72f, 0.26f, 0.26f, 1.0f);
        stage->button_labels[i].text_color = selected
            ? mel_vec4(1.0f, 0.95f, 0.70f, 1.0f)
            : mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

static void street_carlos_pause_stage_apply(Street_Carlos_Pause_Stage* stage, u32 item)
{
    switch (item)
    {
        case STREET_CARLOS_PAUSE_RESUME:
            mel_stage_disable(&stage->stage);
            break;
        case STREET_CARLOS_PAUSE_CHAR_SELECT:
            street_carlos_show_char_select(stage->ctx);
            break;
        case STREET_CARLOS_PAUSE_MAIN_MENU:
            street_carlos_show_main_menu(stage->ctx);
            break;
        case STREET_CARLOS_PAUSE_TITLE:
            street_carlos_show_title(stage->ctx);
            break;
    }
}

static void street_carlos_pause_stage_button_click(void* user_data)
{
    Street_Carlos_Pause_Button_User* user = user_data;
    user->stage->selected_index = user->item;
    street_carlos_pause_stage_sync_buttons(user->stage);
    street_carlos_pause_stage_apply(user->stage, user->item);
}

static void street_carlos_pause_stage_layout(Street_Carlos_Pause_Stage* stage)
{
    mel_widget_set_position(&stage->backdrop.base, mel_vec2(0.0f, 0.0f));
    mel_widget_set_size(&stage->backdrop.base, mel_vec2((f32)GAME_W, (f32)GAME_H));

    mel_widget_set_position(&stage->card.base, mel_vec2(92.0f, 34.0f));
    mel_widget_set_size(&stage->card.base, mel_vec2(200.0f, 164.0f));

    mel_widget_set_position(&stage->title.base, mel_vec2(140.0f, 48.0f));
    mel_widget_set_size(&stage->title.base, mel_vec2(120.0f, 16.0f));

    for (u32 i = 0; i < STREET_CARLOS_PAUSE_ITEM_COUNT; i++)
    {
        f32 y = 74.0f + (f32)i * 28.0f;
        mel_widget_set_position(&stage->buttons[i].base, mel_vec2(108.0f, y));
        mel_widget_set_size(&stage->buttons[i].base, mel_vec2(168.0f, 22.0f));
        mel_widget_set_position(&stage->button_labels[i].base, mel_vec2(128.0f, y + 4.0f));
        mel_widget_set_size(&stage->button_labels[i].base, mel_vec2(128.0f, 14.0f));
    }
}

static void street_carlos_pause_stage_start(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    Street_Carlos_Pause_Stage* stage = user;
    stage->selected_index = STREET_CARLOS_PAUSE_RESUME;
    mel_widget_set_visible(&stage->backdrop.base, true);
    street_carlos_pause_stage_sync_buttons(stage);
    street_carlos_fight_stage_set_paused(stage->ctx->fight_stage, true);
}

static void street_carlos_pause_stage_end(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    Street_Carlos_Pause_Stage* stage = user;
    street_carlos_fight_stage_set_paused(stage->ctx->fight_stage, false);
    mel_widget_set_visible(&stage->backdrop.base, false);
}

void street_carlos_pause_stage_init(Street_Carlos_Pause_Stage* stage, Street_Carlos_Ctx* ctx)
{
    *stage = (Street_Carlos_Pause_Stage){ .ctx = ctx };

    mel_wpanel_init(&stage->backdrop);
    stage->backdrop.color = mel_vec4(0.0f, 0.0f, 0.0f, 0.45f);
    mel_widget_set_visible(&stage->backdrop.base, false);

    mel_wpanel_init(&stage->card);
    stage->card.color = mel_vec4(0.05f, 0.05f, 0.08f, 0.96f);
    mel_widget_add_child(&stage->backdrop.base, &stage->card.base);

    mel_wlabel_init(&stage->title);
    stage->title.font_pool = &ctx->font_pool;
    stage->title.font = ctx->title_font;
    stage->title.text_color = mel_vec4(1.0f, 0.35f, 0.35f, 1.0f);
    mel_wlabel_set_text(&stage->title, S8("PAUSED"));
    mel_widget_add_child(&stage->backdrop.base, &stage->title.base);

    for (u32 i = 0; i < STREET_CARLOS_PAUSE_ITEM_COUNT; i++)
    {
        mel_wbutton_init(&stage->buttons[i]);
        stage->button_users[i] = (Street_Carlos_Pause_Button_User){ .stage = stage, .item = i };
        stage->buttons[i].on_click = street_carlos_pause_stage_button_click;
        stage->buttons[i].click_data = &stage->button_users[i];
        mel_widget_add_child(&stage->backdrop.base, &stage->buttons[i].base);

        mel_wlabel_init(&stage->button_labels[i]);
        stage->button_labels[i].font_pool = &ctx->font_pool;
        stage->button_labels[i].font = ctx->ui_font;
        mel_wlabel_set_text(&stage->button_labels[i], s_pause_labels[i]);
        mel_widget_add_child(&stage->backdrop.base, &stage->button_labels[i].base);
    }

    street_carlos_pause_stage_layout(stage);
    street_carlos_pause_stage_sync_buttons(stage);

    mel_render_stage_2d_widget_layer_init(&ctx->render_stage, &stage->ui_layer,
        .name = S8("pause_ui"),
        .root = &stage->backdrop.base,
        .layer = MEL_RENDER_STAGE_2D_LAYER_UI,
        .alloc = mel_alloc_heap());

    mel_stage_init(&stage->stage,
        .on_start = street_carlos_pause_stage_start,
        .on_end = street_carlos_pause_stage_end,
        .user = stage,
        .start_enabled = false);
}

void street_carlos_pause_stage_shutdown(Street_Carlos_Pause_Stage* stage)
{
    mel_render_stage_2d_widget_layer_shutdown(&stage->ctx->render_stage, &stage->ui_layer);
    mel_widget_destroy(&stage->backdrop.base);
    mel_stage_shutdown(&stage->stage);
}

bool street_carlos_pause_stage_handle_event(Street_Carlos_Pause_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event)
{
    bool fight_live = street_carlos_fight_stage_is_live(ctx->fight_stage);

    if (!mel_stage_is_enabled(&stage->stage))
    {
        if (fight_live && event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE && !event->key.repeat)
        {
            mel_stage_enable(&stage->stage);
            return true;
        }
        return false;
    }

    if (mel_render_stage_2d_widget_layer_process_event(&stage->ui_layer, event))
        return true;

    if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat)
        return true;

    if (event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        mel_stage_disable(&stage->stage);
        return true;
    }
    if (event->key.scancode == SDL_SCANCODE_UP)
    {
        stage->selected_index = (stage->selected_index + STREET_CARLOS_PAUSE_ITEM_COUNT - 1) % STREET_CARLOS_PAUSE_ITEM_COUNT;
        street_carlos_pause_stage_sync_buttons(stage);
        return true;
    }
    if (event->key.scancode == SDL_SCANCODE_DOWN)
    {
        stage->selected_index = (stage->selected_index + 1) % STREET_CARLOS_PAUSE_ITEM_COUNT;
        street_carlos_pause_stage_sync_buttons(stage);
        return true;
    }
    if (event->key.scancode == SDL_SCANCODE_RETURN)
    {
        street_carlos_pause_stage_apply(stage, stage->selected_index);
        return true;
    }

    return true;
}
