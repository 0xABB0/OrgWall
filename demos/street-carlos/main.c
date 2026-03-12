#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <volk.h>
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "string.str8.h"
#include "sprite.pass.h"
#include "render.camera.h"
#include "render.list.h"
#include "render.stage.2d.h"
#include "texture.pool.h"
#include "gpu.texture.h"
#include "gpu.pipeline.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "sim.ctx.h"
#include "input.stack.h"
#include "input.bindings.h"
#include "async.task.h"
#include "async.job.h"
#include "progress.h"

#include "stage.h"
#include "../../melody/stage.h"
#include "actions.h"
#include "mugen.match.h"
#include "mugen.roster.h"
#include "mugen.sff.h"
#include "mugen.fightdef.h"
#include "mugen.hud.h"
#include "game_draw.h"
#include "game_test.h"
#include "vfs.h"
#include "async.io.h"
#include "font.atlas.h"
#include "math.scalar.h"
#include <string.h>

#define SCREEN_TITLE   0
#define SCREEN_LOADING 1
#define SCREEN_FIGHT   2
#define FIGHT_SIMULATED_LOAD_MS 900

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;

static Mel_Camera s_game_camera;
static Mel_Render_Stage_2D s_render_stage;
static Mel_Render_List s_world_list;
static Mel_Render_List s_hud_list;
static Mel_Render_List s_debug_list;

static Mel_Sim_Ctx s_task_sim;
static u8 s_task_event_buf[256];

typedef struct {
    Mugen_Match* match;
    u32 player_index;
    Mugen_Player_Inputs inputs;
} Match_Input_User;

typedef enum {
    FIGHT_PREP_IDLE,
    FIGHT_PREP_WAIT_ROSTER,
    FIGHT_PREP_WAIT_BUILD,
    FIGHT_PREP_UPLOAD_CHAR,
    FIGHT_PREP_UPLOAD_STAGE,
    FIGHT_PREP_UPLOAD_HUD,
    FIGHT_PREP_READY,
    FIGHT_PREP_FAILED,
} Fight_Prep_Phase;

typedef struct {
    Fight_Prep_Phase phase;
    f32              progress;
} Fight_Prep_State;

typedef struct {
    Mel_Input_Stack    input_stack;
    Mel_Input_Bindings p1_bindings;
    Mel_Input_Bindings p2_bindings;
    Match_Input_User   p1_input_user;
    Match_Input_User   p2_input_user;

    Mugen_Match*       match;
    bool               show_hitboxes;
    bool               show_tests;
    bool               initialized;

    Mugen_Char*        ch;
    Mel_Gpu_Texture    char_tex;
    Mel_Texture_Handle char_tex_handle;

    Mugen_Stage        stage;
    Mugen_Sff          stage_sff;
    Mel_Gpu_Texture    stage_tex;
    Mel_Texture_Handle stage_tex_handle;
    bool               stage_loaded;

    Mugen_Fightdef     fightdef;
    Mugen_Sff          fight_sff;
    Mel_Gpu_Texture    fight_tex;
    Mel_Texture_Handle fight_tex_handle;
    Mugen_Hud          hud;
    bool               hud_loaded;

    Fight_Prep_State   prep;
    u64                prep_started_at_ticks;
    Mel_Job            build_job;
    bool               build_failed;
    char               build_error[128];
} Fight_Stage;

static void screen_leave_fight(Fight_Stage* fight);

static Mel_Io s_io;
static Mel_Vfs s_vfs;
static Mugen_Roster s_roster;
static Fight_Stage s_fight;
static Mel_Job_Context* s_job_ctx;

static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Font_Handle s_title_font;
static Mel_Font_Handle s_body_font;

static u32 s_current_screen;
static Mel_Task_Ctx* s_task_ctx;
static Mel_Task_Handle s_load_task;
static Mel_Progress s_load_progress;
static Mel_Stage s_title_stage;
static Mel_Loading_Stage s_loading_stage;
static Mel_Stage s_fight_stage;

static void street_carlos_imgui(void* user)
{
    MEL_UNUSED(user);
    if (s_current_screen == SCREEN_FIGHT && s_fight.show_tests)
        game_test_imgui();
}

static void match_input_apply_action(Mugen_Player_Inputs* inputs, u32 action, bool pressed)
{
    assert(inputs);

    switch (action)
    {
        case ACT_MOVE_LEFT:  inputs->left  = pressed; break;
        case ACT_MOVE_RIGHT: inputs->right = pressed; break;
        case ACT_CROUCH:     inputs->down  = pressed; break;
        case ACT_JUMP:       inputs->up    = pressed; break;
        case ACT_BTN_A:      inputs->a     = pressed; break;
        case ACT_BTN_B:      inputs->b     = pressed; break;
        case ACT_BTN_C:      inputs->c     = pressed; break;
        case ACT_BTN_X:      inputs->x     = pressed; break;
        case ACT_BTN_Y:      inputs->y     = pressed; break;
        case ACT_BTN_Z:      inputs->z     = pressed; break;
    }
}

static bool match_input_on_action(Mel_Input_Action action, f32 value, void* user)
{
    Match_Input_User* input_user = user;
    bool pressed = value > 0.5f;
    match_input_apply_action(&input_user->inputs, action, pressed);
    if (input_user->match)
        mugen_match_set_player_inputs(input_user->match, input_user->player_index, input_user->inputs);
    return action >= ACT_MOVE_LEFT && action <= ACT_BTN_Z;
}

static void init_input(Fight_Stage* fight)
{
    mel_input_bindings_init(&fight->p1_bindings);
    mel_input_bindings_add(&fight->p1_bindings, SDL_SCANCODE_A, ACT_MOVE_LEFT);
    mel_input_bindings_add(&fight->p1_bindings, SDL_SCANCODE_D, ACT_MOVE_RIGHT);
    mel_input_bindings_add(&fight->p1_bindings, SDL_SCANCODE_S, ACT_CROUCH);
    mel_input_bindings_add(&fight->p1_bindings, SDL_SCANCODE_W, ACT_JUMP);
    mel_input_bindings_add(&fight->p1_bindings, SDL_SCANCODE_U, ACT_BTN_X);
    mel_input_bindings_add(&fight->p1_bindings, SDL_SCANCODE_I, ACT_BTN_Y);
    mel_input_bindings_add(&fight->p1_bindings, SDL_SCANCODE_O, ACT_BTN_Z);
    mel_input_bindings_add(&fight->p1_bindings, SDL_SCANCODE_J, ACT_BTN_A);
    mel_input_bindings_add(&fight->p1_bindings, SDL_SCANCODE_K, ACT_BTN_B);
    mel_input_bindings_add(&fight->p1_bindings, SDL_SCANCODE_L, ACT_BTN_C);

    mel_input_bindings_init(&fight->p2_bindings);
    mel_input_bindings_add(&fight->p2_bindings, SDL_SCANCODE_LEFT, ACT_MOVE_LEFT);
    mel_input_bindings_add(&fight->p2_bindings, SDL_SCANCODE_RIGHT, ACT_MOVE_RIGHT);
    mel_input_bindings_add(&fight->p2_bindings, SDL_SCANCODE_DOWN, ACT_CROUCH);
    mel_input_bindings_add(&fight->p2_bindings, SDL_SCANCODE_UP, ACT_JUMP);
    mel_input_bindings_add(&fight->p2_bindings, SDL_SCANCODE_KP_7, ACT_BTN_X);
    mel_input_bindings_add(&fight->p2_bindings, SDL_SCANCODE_KP_8, ACT_BTN_Y);
    mel_input_bindings_add(&fight->p2_bindings, SDL_SCANCODE_KP_9, ACT_BTN_Z);
    mel_input_bindings_add(&fight->p2_bindings, SDL_SCANCODE_KP_4, ACT_BTN_A);
    mel_input_bindings_add(&fight->p2_bindings, SDL_SCANCODE_KP_5, ACT_BTN_B);
    mel_input_bindings_add(&fight->p2_bindings, SDL_SCANCODE_KP_6, ACT_BTN_C);

    fight->p1_input_user = (Match_Input_User){
        .match = fight->match,
        .player_index = MUGEN_MATCH_PLAYER_1,
    };
    fight->p2_input_user = (Match_Input_User){
        .match = fight->match,
        .player_index = MUGEN_MATCH_PLAYER_2,
    };
    mugen_match_set_inputs(fight->match, fight->p1_input_user.inputs, fight->p2_input_user.inputs);

    mel_input_stack_init(&fight->input_stack);

    mel_input_stack_push(&fight->input_stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &fight->p1_bindings,
        .on_action = match_input_on_action,
        .user = &fight->p1_input_user);

    mel_input_stack_push(&fight->input_stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &fight->p2_bindings,
        .on_action = match_input_on_action,
        .user = &fight->p2_input_user);
}

static void release_fight_assets(Fight_Stage* fight)
{
    if (fight->build_job)
    {
        mel_job_wait_and_del(s_job_ctx, fight->build_job);
        fight->build_job = nullptr;
    }

    Mel_Gpu_Device* dev = mel_gpu_dev();

    if (mel_slotmap_handle_valid(fight->char_tex_handle.handle))
        mel_gpu_texture_shutdown(&fight->char_tex, dev);
    fight->char_tex_handle = MEL_TEXTURE_HANDLE_NULL;
    fight->ch = NULL;

    if (mel_slotmap_handle_valid(fight->stage_tex_handle.handle))
        mel_gpu_texture_shutdown(&fight->stage_tex, dev);
    if (fight->stage_sff.atlas_pixels)
        mugen_sff_shutdown(&fight->stage_sff, mel_alloc_heap());
    fight->stage_loaded = false;
    fight->stage_tex_handle = MEL_TEXTURE_HANDLE_NULL;
    mugen_stage_shutdown(&fight->stage, mel_alloc_heap());

    if (mel_slotmap_handle_valid(fight->fight_tex_handle.handle))
        mel_gpu_texture_shutdown(&fight->fight_tex, dev);
    if (fight->fight_sff.atlas_pixels)
        mugen_sff_shutdown(&fight->fight_sff, mel_alloc_heap());
    fight->hud_loaded = false;
    fight->fight_tex_handle = MEL_TEXTURE_HANDLE_NULL;
    mugen_fightdef_shutdown(&fight->fightdef, mel_alloc_heap());
}

static void load_stage(Fight_Stage* fight)
{
    str8 stage_data = mel_vfs_read_text_alloc(&s_vfs, S8("/stages/kfm.def"), mel_alloc_heap());
    if (stage_data.len == 0)
    {
        SDL_Log("Failed to read stage def, using defaults");
        mugen_stage_load(&fight->stage, (str8){0}, mel_alloc_heap());
        return;
    }

    mugen_stage_load(&fight->stage, stage_data, mel_alloc_heap());
    mel_dealloc(mel_alloc_heap(), stage_data.data);

    if (fight->stage.spr_path.len > 0)
    {
        str8 spr_full = str8_fmt_alloc(mel_alloc_heap(), "/stages/%.*s",
            (int)fight->stage.spr_path.len, (char*)fight->stage.spr_path.data);

        if (mugen_sff_load(&fight->stage_sff, &s_vfs, spr_full, mel_alloc_heap()))
        {
            SDL_Log("Stage loaded: %d BG layers, spr=%.*s",
                fight->stage.bg_count, (int)fight->stage.spr_path.len, (char*)fight->stage.spr_path.data);
        }
        else
        {
            SDL_Log("Failed to load stage SFF: %.*s", (int)spr_full.len, (char*)spr_full.data);
        }

        mel_dealloc(mel_alloc_heap(), spr_full.data);
    }
}

static void load_fight_hud(Fight_Stage* fight)
{
    str8 fdef_data = mel_vfs_read_text_alloc(&s_vfs, S8("/data/fight.def"), mel_alloc_heap());
    if (fdef_data.len > 0)
    {
        mugen_fightdef_load(&fight->fightdef, fdef_data, mel_alloc_heap());
        mel_dealloc(mel_alloc_heap(), fdef_data.data);

        if (fight->fightdef.files.sff.len > 0)
        {
            str8 sff_path = str8_fmt_alloc(mel_alloc_heap(), "/data/%.*s",
                (int)fight->fightdef.files.sff.len, (char*)fight->fightdef.files.sff.data);

            if (mugen_sff_load(&fight->fight_sff, &s_vfs, sff_path, mel_alloc_heap()))
            {
                SDL_Log("Fight HUD loaded: sff=%.*s",
                    (int)fight->fightdef.files.sff.len, (char*)fight->fightdef.files.sff.data);
            }
            else
            {
                SDL_Log("Failed to load fight SFF: %.*s", (int)sff_path.len, (char*)sff_path.data);
            }

            mel_dealloc(mel_alloc_heap(), sff_path.data);
        }
    }
    else
    {
        SDL_Log("No fight.def found, HUD disabled");
    }
}

static void upload_fight_char(Fight_Stage* fight)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_texture_init(&fight->char_tex, dev,
        .pixels = fight->ch->sff.atlas_pixels,
        .width = fight->ch->sff.atlas_width,
        .height = fight->ch->sff.atlas_height,
        .nearest_filter = true);
    fight->char_tex.descriptor = mel_gpu_pipeline_alloc_descriptor(&mel_sprite_pass()->pipeline, dev);
    mel_gpu_pipeline_write_texture(&mel_sprite_pass()->pipeline, dev,
        fight->char_tex.descriptor, fight->char_tex.image.view, fight->char_tex.sampler);
    fight->char_tex_handle = mel_texture_pool_register(mel_texture_pool(), &fight->char_tex);
}

static void upload_fight_stage(Fight_Stage* fight)
{
    if (!fight->stage_sff.atlas_pixels)
        return;

    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_texture_init(&fight->stage_tex, dev,
        .pixels = fight->stage_sff.atlas_pixels,
        .width = fight->stage_sff.atlas_width,
        .height = fight->stage_sff.atlas_height,
        .nearest_filter = true);
    fight->stage_tex.descriptor = mel_gpu_pipeline_alloc_descriptor(&mel_sprite_pass()->pipeline, dev);
    mel_gpu_pipeline_write_texture(&mel_sprite_pass()->pipeline, dev,
        fight->stage_tex.descriptor, fight->stage_tex.image.view, fight->stage_tex.sampler);
    fight->stage_tex_handle = mel_texture_pool_register(mel_texture_pool(), &fight->stage_tex);
    fight->stage_loaded = true;
}

static void upload_fight_hud(Fight_Stage* fight)
{
    if (!fight->fight_sff.atlas_pixels)
        return;

    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_texture_init(&fight->fight_tex, dev,
        .pixels = fight->fight_sff.atlas_pixels,
        .width = fight->fight_sff.atlas_width,
        .height = fight->fight_sff.atlas_height,
        .nearest_filter = true);
    fight->fight_tex.descriptor = mel_gpu_pipeline_alloc_descriptor(&mel_sprite_pass()->pipeline, dev);
    mel_gpu_pipeline_write_texture(&mel_sprite_pass()->pipeline, dev,
        fight->fight_tex.descriptor, fight->fight_tex.image.view, fight->fight_tex.sampler);
    fight->fight_tex_handle = mel_texture_pool_register(mel_texture_pool(), &fight->fight_tex);

    fight->hud = (Mugen_Hud){
        .fightdef = &fight->fightdef,
        .fight_sff = &fight->fight_sff,
        .fight_tex = fight->fight_tex_handle,
        .font_pool = &s_font_pool,
        .font = s_body_font,
        .p1_mid_ratio = 1.0f,
        .p2_mid_ratio = 1.0f,
        .p1_power_mid = 0.0f,
        .p2_power_mid = 0.0f,
        .scale_x = (f32)GAME_W / (f32)fight->stage.localcoord_w,
        .scale_y = (f32)GAME_H / (f32)fight->stage.localcoord_h,
    };
    fight->hud_loaded = true;
}

static void fight_build_job(i32 range_start, i32 range_end, i32 thread_index, void* user)
{
    MEL_UNUSED(range_start);
    MEL_UNUSED(range_end);
    MEL_UNUSED(thread_index);

    Fight_Stage* fight = user;
    fight->build_failed = false;
    fight->build_error[0] = 0;

    load_stage(fight);
    load_fight_hud(fight);
}

static Mel_Progress_Status fight_prep_progress(void* user)
{
    Fight_Stage* fight = user;
    f32 progress = fight->prep.progress;

#if FIGHT_SIMULATED_LOAD_MS > 0
    u64 elapsed_ticks = SDL_GetTicks() - fight->prep_started_at_ticks;
    f32 simulated_progress = mel_clampf((f32)elapsed_ticks / (f32)FIGHT_SIMULATED_LOAD_MS, 0.0f, 1.0f);
    progress = mel_minf(progress, simulated_progress);
#endif

    return (Mel_Progress_Status){
        .value = progress,
        .failed = fight->prep.phase == FIGHT_PREP_FAILED,
    };
}

static void fight_prep_fail(Fight_Stage* fight, const char* message)
{
    SDL_Log("%s", message);

    fight->prep.phase = FIGHT_PREP_FAILED;
    fight->build_job = nullptr;
    release_fight_assets(fight);

    if (mel_slotmap_handle_valid(s_load_task))
    {
        mel_task_release(s_task_ctx, s_load_task);
        s_load_task = MEL_TASK_HANDLE_NULL;
    }

    mel_stage_enable(&s_title_stage);
    mel_stage_detach(&s_loading_stage.stage);
}

static void fight_prep_begin(Fight_Stage* fight)
{
    screen_leave_fight(fight);
    fight->prep = (Fight_Prep_State){
        .phase = FIGHT_PREP_WAIT_ROSTER,
        .progress = 0.0f,
    };
    fight->prep_started_at_ticks = SDL_GetTicks();
    fight->build_job = nullptr;
    fight->build_failed = false;
    fight->build_error[0] = 0;
}

static void fight_prep_tick(Fight_Stage* fight)
{
    switch (fight->prep.phase) {
    case FIGHT_PREP_IDLE:
    case FIGHT_PREP_READY:
    case FIGHT_PREP_FAILED:
        return;

    case FIGHT_PREP_WAIT_ROSTER: {
        if (!mel_slotmap_handle_valid(s_load_task))
        {
            fight_prep_fail(fight, "Roster loading task disappeared");
            return;
        }

        u32 load_status = mel_task_status(s_task_ctx, s_load_task);
        if (load_status == MEL_TASK_STATUS_BUILDING || load_status == MEL_TASK_STATUS_RUNNING)
            return;

        if (load_status != MEL_TASK_STATUS_DONE)
        {
            fight_prep_fail(fight, "Roster loading failed!");
            return;
        }

        fight->ch = mugen_roster_find(&s_roster, S8("kfm"));
        if (!fight->ch)
        {
            fight_prep_fail(fight, "Character 'kfm' not found in roster!");
            return;
        }

        fight->build_job = mel_job_dispatch(s_job_ctx, 1, fight_build_job, fight);
        fight->prep.progress = 0.2f;
        fight->prep.phase = FIGHT_PREP_WAIT_BUILD;
    } break;

    case FIGHT_PREP_WAIT_BUILD:
        if (!fight->build_job)
        {
            fight_prep_fail(fight, "Fight build job did not start");
            return;
        }

        if (!mel_job_test_and_del(s_job_ctx, fight->build_job))
            return;

        fight->build_job = nullptr;
        if (fight->build_failed)
        {
            fight_prep_fail(fight, fight->build_error[0] ? fight->build_error : "Fight build job failed");
            return;
        }

        fight->prep.progress = 0.6f;
        fight->prep.phase = FIGHT_PREP_UPLOAD_CHAR;
        break;

    case FIGHT_PREP_UPLOAD_CHAR:
        upload_fight_char(fight);
        fight->prep.progress = 0.75f;
        fight->prep.phase = FIGHT_PREP_UPLOAD_STAGE;
        break;

    case FIGHT_PREP_UPLOAD_STAGE:
        upload_fight_stage(fight);
        fight->prep.progress = 0.9f;
        fight->prep.phase = FIGHT_PREP_UPLOAD_HUD;
        break;

    case FIGHT_PREP_UPLOAD_HUD:
        upload_fight_hud(fight);
        fight->prep.progress = 1.0f;
        fight->prep.phase = FIGHT_PREP_READY;
        break;
    }
}

static void screen_enter_fight(Fight_Stage* fight)
{
    assert(fight->ch != NULL);

    fight->match = mugen_match_create(
        .p1_char = fight->ch,
        .p2_char = fight->ch,
        .stage = &fight->stage,
        .screen_w = (f32)GAME_W,
        .alloc = mel_alloc_heap());

    mugen_match_start(fight->match);
    mel_register_sim(&fight->match->sim);

    init_input(fight);
    fight->initialized = true;
    s_current_screen = SCREEN_FIGHT;

    SDL_Log("FIGHT! P1: WASD + UIO(xyz) JKL(abc)  P2: Arrows + Numpad  Tab: hitboxes  T: tests  ESC: quit");
}

static void screen_leave_fight(Fight_Stage* fight)
{
    if (fight->initialized)
    {
        mel_input_stack_shutdown(&fight->input_stack);
        mel_input_bindings_shutdown(&fight->p1_bindings);
        mel_input_bindings_shutdown(&fight->p2_bindings);
        mel_unregister_sim(&fight->match->sim);
        mugen_match_end(fight->match);
        fight->match = NULL;
    }

    release_fight_assets(fight);
    fight->show_hitboxes = false;
    fight->show_tests = false;
    fight->initialized = false;
    fight->prep = (Fight_Prep_State){0};
}

static void title_stage_start(Mel_Stage* stage, void* user)
{
    MEL_UNUSED(stage);
    MEL_UNUSED(user);

    s_current_screen = SCREEN_TITLE;
    SDL_Log("Street Carlos - Title Screen");
}

static void loading_stage_start(Mel_Stage* stage, void* user)
{
    MEL_UNUSED(stage);
    MEL_UNUSED(user);

    s_current_screen = SCREEN_LOADING;
}

static void fight_stage_start(Mel_Stage* stage, void* user)
{
    Fight_Stage* fight = user;

    if (mel_slotmap_handle_valid(s_load_task))
    {
        u32 load_status = mel_task_status(s_task_ctx, s_load_task);
        mel_task_release(s_task_ctx, s_load_task);
        s_load_task = MEL_TASK_HANDLE_NULL;

        if (load_status != MEL_TASK_STATUS_DONE)
        {
            SDL_Log("Roster loading failed!");
            mel_stage_enable(&s_title_stage);
            mel_stage_detach(stage);
            return;
        }
    }

    if (fight->prep.phase != FIGHT_PREP_READY || fight->ch == NULL)
    {
        SDL_Log("Fight stage enabled before preparation was ready");
        mel_stage_enable(&s_title_stage);
        mel_stage_detach(stage);
        return;
    }

    screen_enter_fight(fight);
}

static void fight_stage_end(Mel_Stage* stage, void* user)
{
    MEL_UNUSED(stage);
    screen_leave_fight(user);
}

static void screen_start_loading(void)
{
    if (mel_slotmap_handle_valid(s_load_task))
        return;

    s_load_task = mugen_roster_load(&s_roster, .folder_path = S8("/chars/"));
    fight_prep_begin(&s_fight);

    mel_progress_clear(&s_load_progress);
    mel_progress_add_task(&s_load_progress, s_task_ctx, s_load_task, 1.0f);
    mel_progress_add_custom(&s_load_progress, fight_prep_progress, &s_fight, 1.0f);

    mel_stage_enable(&s_loading_stage.stage);
    mel_stage_detach(&s_title_stage);
}

static void draw_title(Mel_Render_List* list)
{
    str8 title = S8("STREET CARLOS");
    Mel_Vec2 title_sz = mel_font_atlas_measure_text(&s_font_pool, s_title_font, title);
    f32 title_x = (f32)GAME_W * 0.5f - title_sz.x * 0.5f;
    f32 title_y = 60.0f;
    mel_font_atlas_draw_text(&s_font_pool, s_title_font, list, title, title_x, title_y, mel_vec4(1.0f, 0.3f, 0.3f, 1.0f));

    str8 prompt = S8("Press ENTER to fight");
    Mel_Vec2 prompt_sz = mel_font_atlas_measure_text(&s_font_pool, s_body_font, prompt);
    f32 prompt_x = (f32)GAME_W * 0.5f - prompt_sz.x * 0.5f;
    f32 prompt_y = title_y + title_sz.y + 30.0f;

    f32 t = (f32)SDL_GetTicks() / 1000.0f;
    f32 alpha = 0.5f + 0.5f * SDL_sinf(t * 3.0f);
    mel_font_atlas_draw_text(&s_font_pool, s_body_font, list, prompt, prompt_x, prompt_y, mel_vec4(1.0f, 1.0f, 1.0f, alpha));
}

static void draw_loading(Mel_Render_List* list)
{
    str8 text = S8("Loading...");
    Mel_Vec2 text_sz = mel_font_atlas_measure_text(&s_font_pool, s_body_font, text);
    f32 text_x = (f32)GAME_W * 0.5f - text_sz.x * 0.5f;
    f32 text_y = (f32)GAME_H * 0.5f - 20.0f;
    mel_font_atlas_draw_text(&s_font_pool, s_body_font, list, text,
        text_x, text_y, mel_vec4(1.0f, 1.0f, 1.0f, 1.0f));

    f32 progress = 0.0f;
    if (mel_stage_is_attached(&s_loading_stage.stage))
        progress = mel_progress_value(&s_load_progress);

    f32 bar_w = 200.0f;
    f32 bar_h = 8.0f;
    f32 bar_x = (f32)GAME_W * 0.5f - bar_w * 0.5f;
    f32 bar_y = text_y + text_sz.y + 12.0f;

    mel_draw_sprite(list, .pos = mel_vec2(bar_x, bar_y),
        .size = mel_vec2(bar_w, bar_h),
        .color = mel_vec4(0.2f, 0.2f, 0.2f, 1.0f));

    if (progress > 0.0f)
    {
        mel_draw_sprite(list, .pos = mel_vec2(bar_x, bar_y),
            .size = mel_vec2(bar_w * progress, bar_h),
            .color = mel_vec4(1.0f, 0.3f, 0.3f, 1.0f));
    }
}

static void produce_world_list(Mel_Render_List* list, void* user)
{
    MEL_UNUSED(user);

    switch (s_current_screen)
    {
        case SCREEN_TITLE:
            draw_title(list);
            break;

        case SCREEN_LOADING:
            draw_loading(list);
            break;

        case SCREEN_FIGHT:
        {
            Fighter* p1 = mugen_match_p1(s_fight.match);
            Fighter* p2 = mugen_match_p2(s_fight.match);
            Mugen_Camera* cam = mugen_match_camera(s_fight.match);
            f32 sx = (f32)GAME_W / (f32)s_fight.stage.localcoord_w;
            f32 sy = (f32)GAME_H / (f32)s_fight.stage.localcoord_h;
            f32 zoff = s_fight.stage.zoffset;

            if (s_fight.stage_loaded)
                game_draw_stage_layer(&s_fight.stage, &s_fight.stage_sff, s_fight.stage_tex_handle, cam, 0, list);

            game_draw_afterimage(&p1->cns_state, s_fight.ch, s_fight.char_tex_handle, cam, zoff, sx, sy, list);
            game_draw_afterimage(&p2->cns_state, s_fight.ch, s_fight.char_tex_handle, cam, zoff, sx, sy, list);
            game_draw_fighter(p1, s_fight.ch, s_fight.char_tex_handle, cam, zoff, sx, sy, list);
            game_draw_fighter(p2, s_fight.ch, s_fight.char_tex_handle, cam, zoff, sx, sy, list);

            for (u32 i = 0; i < p1->helper_count; i++)
                game_draw_helper(&p1->helpers[i], s_fight.ch, s_fight.char_tex_handle, cam, zoff, sx, sy, list);
            for (u32 i = 0; i < p2->helper_count; i++)
                game_draw_helper(&p2->helpers[i], s_fight.ch, s_fight.char_tex_handle, cam, zoff, sx, sy, list);

            if (s_fight.stage_loaded)
                game_draw_stage_layer(&s_fight.stage, &s_fight.stage_sff, s_fight.stage_tex_handle, cam, 1, list);
        } break;

        default:
            break;
    }
}

static void produce_hud_list(Mel_Render_List* list, void* user)
{
    MEL_UNUSED(user);

    if (s_current_screen != SCREEN_FIGHT || !s_fight.hud_loaded)
        return;

    Fighter* p1 = mugen_match_p1(s_fight.match);
    Fighter* p2 = mugen_match_p2(s_fight.match);
    Mugen_Hud_State hud_state = {
        .p1_life_ratio = mel_clampf(p1->cns_state.life / p1->cns_state.lifemax, 0.0f, 1.0f),
        .p2_life_ratio = mel_clampf(p2->cns_state.life / p2->cns_state.lifemax, 0.0f, 1.0f),
        .p1_power_ratio = mel_clampf(p1->cns_state.power / p1->cns_state.powermax, 0.0f, 1.0f),
        .p2_power_ratio = mel_clampf(p2->cns_state.power / p2->cns_state.powermax, 0.0f, 1.0f),
        .p1_life = (i32)p1->cns_state.life,
        .p2_life = (i32)p2->cns_state.life,
        .p1_power = (i32)p1->cns_state.power,
        .p2_power = (i32)p2->cns_state.power,
        .time_count = 99,
    };
    mugen_hud_draw(&s_fight.hud, &hud_state, list);
}

static void produce_debug_list(Mel_Render_List* list, void* user)
{
    MEL_UNUSED(user);

    if (s_current_screen != SCREEN_FIGHT || !s_fight.show_hitboxes)
        return;

    Fighter* p1 = mugen_match_p1(s_fight.match);
    Fighter* p2 = mugen_match_p2(s_fight.match);
    Mugen_Camera* cam = mugen_match_camera(s_fight.match);
    f32 sx = (f32)GAME_W / (f32)s_fight.stage.localcoord_w;
    f32 sy = (f32)GAME_H / (f32)s_fight.stage.localcoord_h;
    f32 zoff = s_fight.stage.zoffset;

    game_draw_debug_boxes(p1, cam, zoff, sx, sy, list);
    game_draw_debug_boxes(p2, cam, zoff, sx, sy, list);

    for (u32 i = 0; i < p1->helper_count; i++)
        game_draw_helper_debug_boxes(&p1->helpers[i], cam, zoff, sx, sy, list);
    for (u32 i = 0; i < p2->helper_count; i++)
        game_draw_helper_debug_boxes(&p2->helpers[i], cam, zoff, sx, sy, list);
}

static void task_sim_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);
    mel_task_tick(s_task_ctx);
    fight_prep_tick(&s_fight);
}

static void init_render(void)
{
    mel_render_list_init(&s_world_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL,
        .alloc = mel_alloc_heap());
    mel_render_list_add_producer(&s_world_list, produce_world_list, NULL);

    s_game_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)GAME_W, 0, (f32)GAME_H, -1, 1),
    };

    mel_render_list_init(&s_hud_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL,
        .alloc = mel_alloc_heap());
    mel_render_list_add_producer(&s_hud_list, produce_hud_list, NULL);

    mel_render_list_init(&s_debug_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL,
        .alloc = mel_alloc_heap());
    mel_render_list_add_producer(&s_debug_list, produce_debug_list, NULL);

    bool ok = mel_render_stage_2d_init(&s_render_stage,
        .name = S8("street_carlos"),
        .swapchain = s_swapchain_handle,
        .world_camera = &s_game_camera,
        .hud_camera = &s_game_camera,
        .debug_camera = &s_game_camera,
        .ui_camera = &s_game_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.0f, 0.0f, 0.0f, 1.0f),
        .design_width = GAME_W,
        .design_height = GAME_H,
        .enable_imgui = true,
        .imgui_fn = street_carlos_imgui,
        .install_as_current_graph = true,
        .dev = mel_gpu_dev(),
        .sprite_pass = mel_sprite_pass(),
        .text_pass = mel_text_pass(),
        .alloc = mel_alloc_heap());
    assert(ok);

    MEL_UNUSED(ok);
    mel_render_stage_2d_attach_sprite_list_to_layer(&s_render_stage, MEL_RENDER_STAGE_2D_LAYER_WORLD, &s_world_list);
    mel_render_stage_2d_attach_sprite_list_to_layer(&s_render_stage, MEL_RENDER_STAGE_2D_LAYER_HUD, &s_hud_list);
    mel_render_stage_2d_attach_sprite_list_to_layer(&s_render_stage, MEL_RENDER_STAGE_2D_LAYER_DEBUG, &s_debug_list);
    ok = mel_render_stage_2d_rebuild(&s_render_stage);
    assert(ok);
}

static void on_init(void)
{
    s_fight = (Fight_Stage){0};

    init_render();

    mel_io_init(&s_io, &(Mel_Io_Desc){ .allocator = mel_alloc_heap(), .worker_count = 0 });
    mel_vfs_init(&s_vfs, &(Mel_Vfs_Desc){ .allocator = mel_alloc_heap(), .io = &s_io });
    mel_vfs_mount_native(&s_vfs, S8("/"), S8("demos/street-carlos"), 0, false);
    mel_vfs_mount_native(&s_vfs, S8("/fonts"), S8("/System/Library/Fonts"), 0, false);
    mel_vfs_mount_native(&s_vfs, S8("/stages"), S8("/Users/gabbo/Downloads/mugen-1.1b1/stages"), 0, false);
    mel_vfs_mount_native(&s_vfs, S8("/data"), S8("/Users/gabbo/Downloads/mugen-1.1b1/data"), 0, false);

    mel_font_atlas_pool_init(&s_font_pool, mel_alloc_heap(), mel_gpu_dev(), &s_vfs, .texture_pool = mel_texture_pool());
    s_title_font = mel_font_atlas_pool_load(&s_font_pool, .path = S8("/fonts/Monaco.ttf"), .size = 24.0f);
    s_body_font = mel_font_atlas_pool_load(&s_font_pool, .path = S8("/fonts/Monaco.ttf"), .size = 10.0f);

    s_job_ctx = mel_job_create_context(mel_alloc_heap());

    Mel_Task_Ctx_Desc task_desc = {
        .alloc = mel_alloc_heap(),
        .io    = &s_io,
        .vfs   = &s_vfs,
        .jobs  = s_job_ctx,
    };
    s_task_ctx = mel_task_ctx_create(&task_desc);

    mugen_roster_init(&s_roster,
        .vfs = &s_vfs,
        .task_ctx = s_task_ctx,
        .stcommon_path = S8("/chars/common1.cns"),
        .alloc = mel_alloc_heap());

    mel_sim_init(&s_task_sim,
        .event_buffer = s_task_event_buf,
        .event_buffer_size = sizeof(s_task_event_buf),
        .alloc = mel_alloc_heap());
    mel_sim_add_variable(&s_task_sim, task_sim_update);
    mel_register_sim(&s_task_sim);

    mel_progress_init(&s_load_progress, mel_alloc_heap());
    mel_stage_init(&s_title_stage,
        .on_start = title_stage_start,
        .start_enabled = false);
    mel_stage_init(&s_fight_stage,
        .on_start = fight_stage_start,
        .on_end = fight_stage_end,
        .user = &s_fight,
        .start_enabled = false);
    mel_loading_stage_init(&s_loading_stage,
        .progress = &s_load_progress,
        .next = &s_fight_stage,
        .ready_at = 1.0f,
        .attach_next = true,
        .enable_next = true,
        .detach_self = true);
    s_loading_stage.stage.on_start = loading_stage_start;

    s_current_screen = SCREEN_TITLE;
    s_load_task = MEL_TASK_HANDLE_NULL;
    mel_stage_enable(&s_title_stage);
    mel_stage_tick();
}

static void app_init(Mel_App* app)
{
    if (app->argc > 1 && strcmp(app->argv[1], "--test") == 0)
    {
        int result = mel_test_main(app->argc, app->argv);
        app->should_quit = true;
        exit(result);
    }

    mel_init(.app_name = S8("Street Carlos"), .enable_validation = true);
    s_window_handle = mel_window_create(S8("Street Carlos"), .width = GAME_W * 3, .height = GAME_H * 3);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_window_handle);
    mel_imgui_init(s_window_handle, &mel_swapchain_registry_get(s_swapchain_handle)->swapchain);

    on_init();
}

static void app_shutdown(Mel_App* app)
{
    MEL_UNUSED(app);

    mel_stage_detach(&s_title_stage);
    mel_stage_detach(&s_loading_stage.stage);
    mel_stage_detach(&s_fight_stage);
    mel_stage_tick();

    if (mel_slotmap_handle_valid(s_load_task))
    {
        mel_task_release(s_task_ctx, s_load_task);
        s_load_task = MEL_TASK_HANDLE_NULL;
    }

    screen_leave_fight(&s_fight);

    mel_loading_stage_shutdown(&s_loading_stage);
    mel_stage_shutdown(&s_fight_stage);
    mel_stage_shutdown(&s_title_stage);
    mel_progress_destroy(&s_load_progress);

    mel_unregister_sim(&s_task_sim);
    mel_sim_shutdown(&s_task_sim);

    mugen_roster_shutdown(&s_roster);

    mel_render_stage_2d_shutdown(&s_render_stage);
    mel_render_list_shutdown(&s_debug_list);
    mel_render_list_shutdown(&s_hud_list);
    mel_render_list_shutdown(&s_world_list);

    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_vfs_unmount(&s_vfs, S8("/"));
    mel_vfs_unmount(&s_vfs, S8("/data"));
    mel_vfs_unmount(&s_vfs, S8("/stages"));
    mel_vfs_unmount(&s_vfs, S8("/fonts"));
    mel_task_ctx_destroy(s_task_ctx);
    mel_job_destroy_context(s_job_ctx, mel_alloc_heap());
    mel_vfs_shutdown(&s_vfs);
    mel_io_shutdown(&s_io);

    SDL_Log("Street Carlos shutdown");
}

static void app_event(Mel_App* app, SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        app->should_quit = true;
        return;
    }

    switch (s_current_screen) {
    case SCREEN_TITLE:
        if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_RETURN && !event->key.repeat)
            screen_start_loading();
        break;

    case SCREEN_LOADING:
        break;

    case SCREEN_FIGHT:
        if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_TAB && !event->key.repeat)
        {
            s_fight.show_hitboxes = !s_fight.show_hitboxes;
            return;
        }
        if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_T && !event->key.repeat)
        {
            s_fight.show_tests = !s_fight.show_tests;
            return;
        }
        mel_input_stack_dispatch(&s_fight.input_stack, event);
        break;
    }
}

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_event = app_event
)
