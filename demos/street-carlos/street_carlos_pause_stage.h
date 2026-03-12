#pragma once

#include "street_carlos_ctx.h"
#include "render.stage.2d.h"
#include "ui.widget.panel.h"
#include "ui.widget.button.h"
#include "ui.widget.label.h"

typedef enum {
    STREET_CARLOS_PAUSE_RESUME = 0,
    STREET_CARLOS_PAUSE_CHAR_SELECT,
    STREET_CARLOS_PAUSE_MAIN_MENU,
    STREET_CARLOS_PAUSE_TITLE,
    STREET_CARLOS_PAUSE_ITEM_COUNT,
} Street_Carlos_Pause_Item;

typedef struct {
    struct Street_Carlos_Pause_Stage* stage;
    u32 item;
} Street_Carlos_Pause_Button_User;

struct Street_Carlos_Pause_Stage {
    Mel_Stage stage;
    Street_Carlos_Ctx* ctx;
    Mel_Render_Stage_2D_Widget_Layer ui_layer;
    Mel_WPanel backdrop;
    Mel_WPanel card;
    Mel_WLabel title;
    Mel_WButton buttons[STREET_CARLOS_PAUSE_ITEM_COUNT];
    Mel_WLabel button_labels[STREET_CARLOS_PAUSE_ITEM_COUNT];
    Street_Carlos_Pause_Button_User button_users[STREET_CARLOS_PAUSE_ITEM_COUNT];
    u32 selected_index;
};

void street_carlos_pause_stage_init(Street_Carlos_Pause_Stage* stage, Street_Carlos_Ctx* ctx);
void street_carlos_pause_stage_shutdown(Street_Carlos_Pause_Stage* stage);
bool street_carlos_pause_stage_handle_event(Street_Carlos_Pause_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event);
