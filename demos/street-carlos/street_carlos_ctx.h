#pragma once

#include <SDL3/SDL.h>

#include "core.app.h"
#include "stage.h"
#include "window.h"
#include "swapchain.h"
#include "render.camera.h"
#include "render.list.h"
#include "render.stage.2d.h"
#include "sim.ctx.h"
#include "progress.h"
#include "../../melody/stage.h"
#include "font.atlas.h"
#include "mugen.roster.h"
#include "string.str8.h"

#define STREET_CARLOS_MAX_STAGE_CHOICES 256

typedef struct Street_Carlos_Title_Stage Street_Carlos_Title_Stage;
typedef struct Street_Carlos_Main_Menu_Stage Street_Carlos_Main_Menu_Stage;
typedef struct Street_Carlos_Char_Select_Stage Street_Carlos_Char_Select_Stage;
typedef struct Street_Carlos_Stage_Select_Stage Street_Carlos_Stage_Select_Stage;
typedef struct Street_Carlos_Loading_Stage Street_Carlos_Loading_Stage;
typedef struct Street_Carlos_Fight_Stage Street_Carlos_Fight_Stage;
typedef struct Street_Carlos_Pause_Stage Street_Carlos_Pause_Stage;
typedef struct Street_Carlos_Console_Stage Street_Carlos_Console_Stage;

typedef struct {
    str8 path;
    str8 label;
} Street_Carlos_Stage_Choice;

typedef struct Street_Carlos_Ctx {
    Mel_Window_Handle window_handle;
    Mel_Swapchain_Handle swapchain_handle;

    Mel_Camera game_camera;
    Mel_Render_Stage_2D render_stage;
    Mel_Render_List world_list;
    Mel_Render_List hud_list;
    Mel_Render_List debug_list;

    Mel_Sim_Ctx task_sim;
    u8 task_event_buf[256];


    Mugen_Roster roster;

    Mel_Font_Atlas_Handle title_font;
    Mel_Font_Atlas_Handle ui_font;
    Mel_Font_Atlas_Handle body_font;

    Mel_Progress load_progress;
    Mel_Stage_Registry stage_registry;

    Street_Carlos_Stage_Choice stage_choices[STREET_CARLOS_MAX_STAGE_CHOICES];
    u32 stage_choice_count;
    u64 menu_rng_state;

    Street_Carlos_Title_Stage* title_stage;
    Street_Carlos_Main_Menu_Stage* main_menu_stage;
    Street_Carlos_Char_Select_Stage* char_select_stage;
    Street_Carlos_Stage_Select_Stage* stage_select_stage;
    Street_Carlos_Loading_Stage* loading_stage;
    Street_Carlos_Fight_Stage* fight_stage;
    Street_Carlos_Pause_Stage* pause_stage;
    Street_Carlos_Console_Stage* console_stage;
} Street_Carlos_Ctx;

enum {
    STREET_CARLOS_STAGE_TAG_FLOW   = 1 << 0,
    STREET_CARLOS_STAGE_TAG_MODAL  = 1 << 1,
    STREET_CARLOS_STAGE_TAG_GLOBAL = 1 << 2,
};
