#pragma once

#include "street_carlos_ctx.h"
#include "render.stage.2d.h"
#include "ui.widget.panel.h"
#include "ui.widget.label.h"
#include "ui.widget.edit.h"

#define STREET_CARLOS_CONSOLE_LINE_COUNT 4
#define STREET_CARLOS_CONSOLE_HISTORY_COUNT 16

struct Street_Carlos_Console_Stage {
    Mel_Stage stage;
    Street_Carlos_Ctx* ctx;
    Mel_Render_Stage_2D_Widget_Layer ui_layer;
    Mel_WPanel panel;
    Mel_WLabel title;
    Mel_WLabel lines[STREET_CARLOS_CONSOLE_LINE_COUNT];
    Mel_WEdit input;
    bool want_open;
    f32 open_t;
    char line_storage[STREET_CARLOS_CONSOLE_LINE_COUNT][128];
    char history[STREET_CARLOS_CONSOLE_HISTORY_COUNT][128];
    u32 history_count;
    i32 history_cursor;
};

void street_carlos_console_stage_init(Street_Carlos_Console_Stage* stage, Street_Carlos_Ctx* ctx);
void street_carlos_console_stage_shutdown(Street_Carlos_Console_Stage* stage);
bool street_carlos_console_stage_handle_event(Street_Carlos_Console_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event);
