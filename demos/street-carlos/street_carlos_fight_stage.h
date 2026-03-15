#pragma once

#include "street_carlos_ctx.h"
#include "input.stack.h"
#include "input.bindings.h"
#include "gpu.texture.h"
#include "texture.pool.h"
#include "mugen.match.h"
#include "mugen.stage.h"
#include "mugen.sff.h"
#include "mugen.fightdef.h"
#include "mugen.hud.h"

typedef struct {
    Mugen_Match* match;
    u32 player_index;
    Mugen_Player_Inputs inputs;
} Street_Carlos_Match_Input_User;

typedef enum {
    STREET_CARLOS_FIGHT_PREP_IDLE,
    STREET_CARLOS_FIGHT_PREP_WAIT_BUILD,
    STREET_CARLOS_FIGHT_PREP_UPLOAD_CHAR,
    STREET_CARLOS_FIGHT_PREP_UPLOAD_STAGE,
    STREET_CARLOS_FIGHT_PREP_UPLOAD_HUD,
    STREET_CARLOS_FIGHT_PREP_READY,
    STREET_CARLOS_FIGHT_PREP_FAILED,
} Street_Carlos_Fight_Prep_Phase;

typedef struct {
    Street_Carlos_Fight_Prep_Phase phase;
    f32 progress;
} Street_Carlos_Fight_Prep_State;

struct Street_Carlos_Fight_Stage {
    Mel_Stage stage;
    Street_Carlos_Ctx* ctx;

    Mel_Input_Stack input_stack;
    Mel_Input_Bindings p1_bindings;
    Mel_Input_Bindings p2_bindings;
    Street_Carlos_Match_Input_User p1_input_user;
    Street_Carlos_Match_Input_User p2_input_user;

    Mugen_Match* match;
    bool show_hitboxes;
    bool show_tests;
    bool show_inputs;
    bool show_moves;
    bool paused;
    bool initialized;

    Mugen_Char* p1_char;
    Mugen_Char* p2_char;
    Mel_Gpu_Texture p1_char_tex;
    Mel_Gpu_Texture p2_char_tex;
    Mel_Texture_Handle p1_char_tex_handle;
    Mel_Texture_Handle p2_char_tex_handle;

    Mugen_Stage stage_def;
    Mugen_Sff stage_sff;
    Mel_Gpu_Texture stage_tex;
    Mel_Texture_Handle stage_tex_handle;
    bool stage_loaded;
    str8 selected_stage_path;

    Mugen_Fightdef fightdef;
    Mugen_Sff fight_sff;
    Mel_Gpu_Texture fight_tex;
    Mel_Texture_Handle fight_tex_handle;
    Mugen_Hud hud;
    bool hud_loaded;

    Street_Carlos_Fight_Prep_State prep;
    u64 prep_started_at_ticks;
};

void street_carlos_fight_stage_init(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx);
void street_carlos_fight_stage_shutdown(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx);
void street_carlos_fight_stage_prepare(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx, Mugen_Char* p1_char, Mugen_Char* p2_char, str8 stage_path);
void street_carlos_fight_stage_tick(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx);
Mel_Progress_Status street_carlos_fight_stage_progress(void* user);
bool street_carlos_fight_stage_handle_event(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event);
void street_carlos_fight_stage_draw_world(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list);
void street_carlos_fight_stage_draw_hud(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list);
void street_carlos_fight_stage_draw_debug(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list);
bool street_carlos_fight_stage_show_imgui(Street_Carlos_Fight_Stage* stage);
bool street_carlos_fight_stage_is_live(Street_Carlos_Fight_Stage* stage);
void street_carlos_fight_stage_set_paused(Street_Carlos_Fight_Stage* stage, bool paused);
void street_carlos_fight_stage_toggle_hitboxes(Street_Carlos_Fight_Stage* stage);
void street_carlos_fight_stage_toggle_tests(Street_Carlos_Fight_Stage* stage);
