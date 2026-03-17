#include "street_carlos_console_stage.h"

#include <string.h>

#include "allocator.heap.h"
#include "street_carlos_flow.h"
#include "street_carlos_fight_stage.h"
#include "street_carlos_pause_stage.h"
#include "math.scalar.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "string.str8.h"

static void street_carlos_console_stage_focus_input(Street_Carlos_Console_Stage* stage)
{
    if (stage->ui_layer.focused)
        stage->ui_layer.focused->state &= ~MEL_WIDGET_STATE_FOCUSED;
    stage->ui_layer.focused = &stage->input.base;
    stage->input.base.state |= MEL_WIDGET_STATE_FOCUSED;
}

static void street_carlos_console_stage_sync_lines(Street_Carlos_Console_Stage* stage)
{
    for (u32 i = 0; i < STREET_CARLOS_CONSOLE_LINE_COUNT; i++)
        mel_wlabel_set_text(&stage->lines[i], str8_from_cstr(stage->line_storage[i]));
}

static void street_carlos_console_stage_push_line(Street_Carlos_Console_Stage* stage, const char* text)
{
    for (u32 i = 0; i + 1 < STREET_CARLOS_CONSOLE_LINE_COUNT; i++)
        memcpy(stage->line_storage[i], stage->line_storage[i + 1], sizeof(stage->line_storage[i]));

    SDL_snprintf(stage->line_storage[STREET_CARLOS_CONSOLE_LINE_COUNT - 1],
        sizeof(stage->line_storage[0]), "%s", text ? text : "");
    street_carlos_console_stage_sync_lines(stage);
}

static void street_carlos_console_stage_printf(Street_Carlos_Console_Stage* stage, const char* prefix, str8 text)
{
    char buf[160];
    SDL_snprintf(buf, sizeof(buf), "%s%.*s", prefix ? prefix : "", (int)text.len, (char*)text.data);
    street_carlos_console_stage_push_line(stage, buf);
}

static void street_carlos_console_stage_layout(Street_Carlos_Console_Stage* stage)
{
    const f32 panel_h = 128.0f;
    f32 panel_y = -panel_h + panel_h * stage->open_t;

    mel_widget_set_position(&stage->panel.base, mel_vec2(0.0f, panel_y));
    mel_widget_set_size(&stage->panel.base, mel_vec2((f32)GAME_W, panel_h));

    mel_widget_set_position(&stage->title.base, mel_vec2(8.0f, panel_y + 8.0f));
    mel_widget_set_size(&stage->title.base, mel_vec2(120.0f, 14.0f));

    for (u32 i = 0; i < STREET_CARLOS_CONSOLE_LINE_COUNT; i++)
    {
        mel_widget_set_position(&stage->lines[i].base, mel_vec2(8.0f, panel_y + 28.0f + (f32)i * 15.0f));
        mel_widget_set_size(&stage->lines[i].base, mel_vec2((f32)GAME_W - 16.0f, 14.0f));
    }

    mel_widget_set_position(&stage->input.base, mel_vec2(8.0f, panel_y + 96.0f));
    mel_widget_set_size(&stage->input.base, mel_vec2((f32)GAME_W - 16.0f, 22.0f));
}

static void street_carlos_console_stage_history_push(Street_Carlos_Console_Stage* stage, str8 command)
{
    if (command.len == 0)
        return;

    if (stage->history_count == STREET_CARLOS_CONSOLE_HISTORY_COUNT)
    {
        for (u32 i = 0; i + 1 < STREET_CARLOS_CONSOLE_HISTORY_COUNT; i++)
            memcpy(stage->history[i], stage->history[i + 1], sizeof(stage->history[i]));
        stage->history_count--;
    }

    SDL_snprintf(stage->history[stage->history_count], sizeof(stage->history[0]),
        "%.*s", (int)command.len, (char*)command.data);
    stage->history_count++;
    stage->history_cursor = (i32)stage->history_count;
}

static void street_carlos_console_stage_history_apply(Street_Carlos_Console_Stage* stage, i32 cursor)
{
    if (stage->history_count == 0)
        return;

    if (cursor < 0)
        cursor = 0;
    if (cursor > (i32)stage->history_count)
        cursor = (i32)stage->history_count;
    stage->history_cursor = cursor;

    if (cursor == (i32)stage->history_count)
    {
        mel_wedit_set_text(&stage->input, STR8_EMPTY);
    }
    else
    {
        mel_wedit_set_text(&stage->input, str8_from_cstr(stage->history[cursor]));
    }
}

static bool street_carlos_console_command_eq(str8 command, const char* literal)
{
    return str8_ieq_cstr(str8_trim(command), literal);
}

static void street_carlos_console_stage_run_command(Street_Carlos_Console_Stage* stage, str8 command)
{
    command = str8_trim(command);
    if (command.len == 0)
        return;

    street_carlos_console_stage_printf(stage, "> ", command);
    street_carlos_console_stage_history_push(stage, command);

    if (street_carlos_console_command_eq(command, "help"))
    {
        street_carlos_console_stage_push_line(stage, "pause resume hitboxes tests");
        street_carlos_console_stage_push_line(stage, "quick_fight main_menu title");
        street_carlos_console_stage_push_line(stage, "clear help");
    }
    else if (street_carlos_console_command_eq(command, "clear"))
    {
        for (u32 i = 0; i < STREET_CARLOS_CONSOLE_LINE_COUNT; i++)
            stage->line_storage[i][0] = '\0';
        street_carlos_console_stage_sync_lines(stage);
    }
    else if (street_carlos_console_command_eq(command, "pause"))
    {
        if (street_carlos_fight_stage_is_live(stage->ctx->fight_stage))
        {
            mel_stage_enable(&stage->ctx->pause_stage->stage);
            street_carlos_console_stage_push_line(stage, "pause enabled");
        }
        else
        {
            street_carlos_console_stage_push_line(stage, "no live fight");
        }
    }
    else if (street_carlos_console_command_eq(command, "resume"))
    {
        mel_stage_disable(&stage->ctx->pause_stage->stage);
        street_carlos_console_stage_push_line(stage, "pause disabled");
    }
    else if (street_carlos_console_command_eq(command, "hitboxes"))
    {
        street_carlos_fight_stage_toggle_hitboxes(stage->ctx->fight_stage);
        street_carlos_console_stage_push_line(stage, "toggled hitboxes");
    }
    else if (street_carlos_console_command_eq(command, "tests"))
    {
        street_carlos_fight_stage_toggle_tests(stage->ctx->fight_stage);
        street_carlos_console_stage_push_line(stage, "toggled tests");
    }
    else if (street_carlos_console_command_eq(command, "quick_fight"))
    {
        street_carlos_start_quick_fight(stage->ctx);
        street_carlos_console_stage_push_line(stage, "starting quick fight");
    }
    else if (street_carlos_console_command_eq(command, "main_menu"))
    {
        street_carlos_show_main_menu(stage->ctx);
        street_carlos_console_stage_push_line(stage, "showing main menu");
    }
    else if (street_carlos_console_command_eq(command, "title"))
    {
        street_carlos_show_title(stage->ctx);
        street_carlos_console_stage_push_line(stage, "showing title");
    }
    else
    {
        street_carlos_console_stage_push_line(stage, "unknown command");
    }

    mel_wedit_set_text(&stage->input, STR8_EMPTY);
    stage->history_cursor = (i32)stage->history_count;
}

static void street_carlos_console_stage_on_confirm(Mel_WEdit* edit, void* user_data)
{
    Street_Carlos_Console_Stage* stage = user_data;
    street_carlos_console_stage_run_command(stage, mel_wedit_get_text(edit));
}

static void street_carlos_console_stage_tick(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    Street_Carlos_Console_Stage* stage = user;
    f32 target = stage->want_open ? 1.0f : 0.0f;
    f32 step = 0.18f;

    if (stage->open_t < target)
        stage->open_t = mel_minf(stage->open_t + step, target);
    else if (stage->open_t > target)
        stage->open_t = mel_maxf(stage->open_t - step, target);

    street_carlos_console_stage_layout(stage);
    mel_widget_set_visible(&stage->panel.base, stage->open_t > 0.0f);

    if (!stage->want_open && stage->open_t <= 0.0f)
        mel_stage_disable(&stage->stage);
}

static void street_carlos_console_stage_start(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    Street_Carlos_Console_Stage* stage = user;
    stage->want_open = true;
    mel_widget_set_visible(&stage->panel.base, true);
    street_carlos_console_stage_focus_input(stage);
}

static void street_carlos_console_stage_end(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    Street_Carlos_Console_Stage* stage = user;
    stage->panel.base.state &= ~MEL_WIDGET_STATE_FOCUSED;
    stage->input.base.state &= ~MEL_WIDGET_STATE_FOCUSED;
    stage->ui_layer.focused = NULL;
    mel_widget_set_visible(&stage->panel.base, false);
}

void street_carlos_console_stage_init(Street_Carlos_Console_Stage* stage, Street_Carlos_Ctx* ctx)
{
    *stage = (Street_Carlos_Console_Stage){ .ctx = ctx };

    mel_wpanel_init(&stage->panel);
    stage->panel.color = mel_vec4(0.02f, 0.02f, 0.03f, 0.92f);
    mel_widget_set_visible(&stage->panel.base, false);

    mel_wlabel_init(&stage->title);
    stage->title.font = ctx->ui_font;
    stage->title.text_color = mel_vec4(0.95f, 0.45f, 0.45f, 1.0f);
    mel_wlabel_set_text(&stage->title, S8("CONSOLE"));
    mel_widget_add_child(&stage->panel.base, &stage->title.base);

    for (u32 i = 0; i < STREET_CARLOS_CONSOLE_LINE_COUNT; i++)
    {
        mel_wlabel_init(&stage->lines[i]);
        stage->lines[i].font = ctx->ui_font;
        stage->lines[i].text_color = mel_vec4(0.88f, 0.88f, 0.90f, 1.0f);
        mel_widget_add_child(&stage->panel.base, &stage->lines[i].base);
    }

    mel_wedit_init(&stage->input);
    stage->input.font = ctx->ui_font;
    stage->input.on_confirm = street_carlos_console_stage_on_confirm;
    stage->input.user_data = stage;
    mel_wedit_set_placeholder(&stage->input, S8("command + Enter"));
    mel_widget_add_child(&stage->panel.base, &stage->input.base);

    street_carlos_console_stage_layout(stage);
    street_carlos_console_stage_push_line(stage, "` toggles console");
    street_carlos_console_stage_push_line(stage, "type help");

    mel_render_stage_2d_widget_layer_init(&ctx->render_stage, &stage->ui_layer,
        .name = S8("console_ui"),
        .root = &stage->panel.base,
        .layer = MEL_RENDER_STAGE_2D_LAYER_UI,
        .alloc = mel_alloc_heap());

    mel_stage_init(&stage->stage,
        .on_start = street_carlos_console_stage_start,
        .on_end = street_carlos_console_stage_end,
        .on_tick = street_carlos_console_stage_tick,
        .user = stage,
        .start_enabled = false);
}

void street_carlos_console_stage_shutdown(Street_Carlos_Console_Stage* stage)
{
    mel_render_stage_2d_widget_layer_shutdown(&stage->ctx->render_stage, &stage->ui_layer);
    mel_widget_destroy(&stage->panel.base);
    mel_stage_shutdown(&stage->stage);
}

bool street_carlos_console_stage_handle_event(Street_Carlos_Console_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event)
{
    MEL_UNUSED(ctx);

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_GRAVE && !event->key.repeat)
    {
        if (mel_stage_is_enabled(&stage->stage))
        {
            stage->want_open = !stage->want_open;
            if (stage->want_open)
                street_carlos_console_stage_focus_input(stage);
        }
        else
        {
            mel_stage_enable(&stage->stage);
        }
        return true;
    }

    if (!mel_stage_is_enabled(&stage->stage))
        return false;

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        if (event->key.scancode == SDL_SCANCODE_UP)
        {
            street_carlos_console_stage_history_apply(stage, stage->history_cursor - 1);
            return true;
        }
        if (event->key.scancode == SDL_SCANCODE_DOWN)
        {
            street_carlos_console_stage_history_apply(stage, stage->history_cursor + 1);
            return true;
        }
    }

    if (mel_render_stage_2d_widget_layer_process_event(&stage->ui_layer, event))
        return true;

    return true;
}
