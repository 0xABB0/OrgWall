#include "street_carlos_fight_stage.h"

#include "street_carlos_flow.h"
#include "street_carlos_loading_stage.h"

#include <assert.h>

#include "actions.h"
#include "allocator.heap.h"
#include "core.engine.h"
#include "game_draw.h"
#include "game_test.h"
#include "gpu.device.h"
#include "gpu.pipeline.h"
#include "math.scalar.h"
#include "mugen.air.h"
#include "sprite.pass.h"
#include "string.path.h"
#include "vfs.h"

#define STREET_CARLOS_FIGHT_SIMULATED_LOAD_MS 900

static void match_input_apply_action(Mugen_Player_Inputs* inputs, u32 action, bool pressed)
{
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
    Street_Carlos_Match_Input_User* input_user = user;
    bool pressed = value > 0.5f;
    match_input_apply_action(&input_user->inputs, action, pressed);
    if (input_user->match)
        mugen_match_set_player_inputs(input_user->match, input_user->player_index, input_user->inputs);
    return action >= ACT_MOVE_LEFT && action <= ACT_BTN_Z;
}

static void street_carlos_fight_stage_init_input(Street_Carlos_Fight_Stage* stage)
{
    mel_input_bindings_init(&stage->p1_bindings);
    mel_input_bindings_add(&stage->p1_bindings, SDL_SCANCODE_A, ACT_MOVE_LEFT);
    mel_input_bindings_add(&stage->p1_bindings, SDL_SCANCODE_D, ACT_MOVE_RIGHT);
    mel_input_bindings_add(&stage->p1_bindings, SDL_SCANCODE_S, ACT_CROUCH);
    mel_input_bindings_add(&stage->p1_bindings, SDL_SCANCODE_W, ACT_JUMP);
    mel_input_bindings_add(&stage->p1_bindings, SDL_SCANCODE_U, ACT_BTN_X);
    mel_input_bindings_add(&stage->p1_bindings, SDL_SCANCODE_I, ACT_BTN_Y);
    mel_input_bindings_add(&stage->p1_bindings, SDL_SCANCODE_O, ACT_BTN_Z);
    mel_input_bindings_add(&stage->p1_bindings, SDL_SCANCODE_J, ACT_BTN_A);
    mel_input_bindings_add(&stage->p1_bindings, SDL_SCANCODE_K, ACT_BTN_B);
    mel_input_bindings_add(&stage->p1_bindings, SDL_SCANCODE_L, ACT_BTN_C);

    mel_input_bindings_init(&stage->p2_bindings);
    mel_input_bindings_add(&stage->p2_bindings, SDL_SCANCODE_LEFT, ACT_MOVE_LEFT);
    mel_input_bindings_add(&stage->p2_bindings, SDL_SCANCODE_RIGHT, ACT_MOVE_RIGHT);
    mel_input_bindings_add(&stage->p2_bindings, SDL_SCANCODE_DOWN, ACT_CROUCH);
    mel_input_bindings_add(&stage->p2_bindings, SDL_SCANCODE_UP, ACT_JUMP);
    mel_input_bindings_add(&stage->p2_bindings, SDL_SCANCODE_KP_7, ACT_BTN_X);
    mel_input_bindings_add(&stage->p2_bindings, SDL_SCANCODE_KP_8, ACT_BTN_Y);
    mel_input_bindings_add(&stage->p2_bindings, SDL_SCANCODE_KP_9, ACT_BTN_Z);
    mel_input_bindings_add(&stage->p2_bindings, SDL_SCANCODE_KP_4, ACT_BTN_A);
    mel_input_bindings_add(&stage->p2_bindings, SDL_SCANCODE_KP_5, ACT_BTN_B);
    mel_input_bindings_add(&stage->p2_bindings, SDL_SCANCODE_KP_6, ACT_BTN_C);

    stage->p1_input_user = (Street_Carlos_Match_Input_User){
        .match = stage->match,
        .player_index = MUGEN_MATCH_PLAYER_1,
    };
    stage->p2_input_user = (Street_Carlos_Match_Input_User){
        .match = stage->match,
        .player_index = MUGEN_MATCH_PLAYER_2,
    };
    mugen_match_set_inputs(stage->match, stage->p1_input_user.inputs, stage->p2_input_user.inputs);

    mel_input_stack_init(&stage->input_stack);
    mel_input_stack_push(&stage->input_stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &stage->p1_bindings,
        .on_action = match_input_on_action,
        .user = &stage->p1_input_user);
    mel_input_stack_push(&stage->input_stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &stage->p2_bindings,
        .on_action = match_input_on_action,
        .user = &stage->p2_input_user);
}

static void street_carlos_fight_stage_release_assets(Street_Carlos_Fight_Stage* stage)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    if (mel_slotmap_handle_valid(stage->p1_char_tex_handle.handle))
        mel_gpu_texture_shutdown(&stage->p1_char_tex, dev);
    if (mel_slotmap_handle_valid(stage->p2_char_tex_handle.handle))
        mel_gpu_texture_shutdown(&stage->p2_char_tex, dev);
    if (mel_slotmap_handle_valid(stage->stage_tex_handle.handle))
        mel_gpu_texture_shutdown(&stage->stage_tex, dev);
    if (mel_slotmap_handle_valid(stage->fight_tex_handle.handle))
        mel_gpu_texture_shutdown(&stage->fight_tex, dev);

    if (stage->stage_sff.atlas_pixels)
        mugen_sff_shutdown(&stage->stage_sff, mel_alloc_heap());
    if (stage->fight_sff.atlas_pixels)
        mugen_sff_shutdown(&stage->fight_sff, mel_alloc_heap());

    mugen_stage_shutdown(&stage->stage_def, mel_alloc_heap());
    mugen_fightdef_shutdown(&stage->fightdef, mel_alloc_heap());

    stage->p1_char_tex_handle = MEL_TEXTURE_HANDLE_NULL;
    stage->p2_char_tex_handle = MEL_TEXTURE_HANDLE_NULL;
    stage->stage_tex_handle = MEL_TEXTURE_HANDLE_NULL;
    stage->fight_tex_handle = MEL_TEXTURE_HANDLE_NULL;
    stage->p1_char = NULL;
    stage->p2_char = NULL;
    stage->selected_stage_path = (str8){0};
    stage->stage_loaded = false;
    stage->hud_loaded = false;
}

static void street_carlos_fight_stage_load_stage_def(Street_Carlos_Fight_Stage* stage)
{
    i64 fsize = 0;
    u8* data = mel_vfs_read_file(stage->selected_stage_path, &fsize, mel_alloc_heap());
    if (data)
    {
        mugen_stage_load(&stage->stage_def, str8_from_parts(data, (size)fsize), mel_alloc_heap());
        mel_dealloc(mel_alloc_heap(), data);
    }
}

static void street_carlos_fight_stage_load_stage_sff(Street_Carlos_Fight_Stage* stage)
{
    if (stage->stage_def.spr_path.len == 0)
        return;

    u8 path_buf[1024];
    str8 stage_dir = mel_path_parent(stage->selected_stage_path);
    str8 spr_full = mel_path_join(stage_dir, stage->stage_def.spr_path, path_buf, sizeof(path_buf));
    mugen_sff_load(&stage->stage_sff, spr_full, mel_alloc_heap());
}

static void street_carlos_fight_stage_load_fightdef(Street_Carlos_Fight_Stage* stage)
{
    str8 path = S8("/data/fight.def");
    i64 fsize = 0;
    u8* data = mel_vfs_read_file(path, &fsize, mel_alloc_heap());
    if (data)
    {
        mugen_fightdef_load(&stage->fightdef, str8_from_parts(data, (size)fsize), mel_alloc_heap());
        mel_dealloc(mel_alloc_heap(), data);
    }
}

static void street_carlos_fight_stage_load_hud_sff(Street_Carlos_Fight_Stage* stage)
{
    if (stage->fightdef.files.sff.len == 0)
        return;

    str8 path = str8_fmt_alloc(mel_alloc_heap(), "/data/%.*s",
        (int)stage->fightdef.files.sff.len, (char*)stage->fightdef.files.sff.data);
    mugen_sff_load(&stage->fight_sff, path, mel_alloc_heap());
    mel_dealloc(mel_alloc_heap(), path.data);
}

static void street_carlos_fight_stage_upload_char_texture(Mugen_Char* ch, Mel_Gpu_Texture* texture, Mel_Texture_Handle* out_handle)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_texture_init(texture, dev,
        .pixels = ch->sff.atlas_pixels,
        .width = ch->sff.atlas_width,
        .height = ch->sff.atlas_height,
        .nearest_filter = true);
    texture->_descriptor = mel_gpu_pipeline_alloc_descriptor(&mel_sprite_pass()->pipeline, dev);
    mel_gpu_pipeline_write_texture(&mel_sprite_pass()->pipeline, dev,
        texture->_descriptor, texture->image._view, texture->_sampler);
    *out_handle = mel_texture_pool_register(mel_texture_pool(), texture);
}

static void street_carlos_fight_stage_upload_chars(Street_Carlos_Fight_Stage* stage)
{
    street_carlos_fight_stage_upload_char_texture(stage->p1_char, &stage->p1_char_tex, &stage->p1_char_tex_handle);
    street_carlos_fight_stage_upload_char_texture(stage->p2_char, &stage->p2_char_tex, &stage->p2_char_tex_handle);
}

static void street_carlos_fight_stage_upload_stage(Street_Carlos_Fight_Stage* stage)
{
    if (!stage->stage_sff.atlas_pixels)
        return;

    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_texture_init(&stage->stage_tex, dev,
        .pixels = stage->stage_sff.atlas_pixels,
        .width = stage->stage_sff.atlas_width,
        .height = stage->stage_sff.atlas_height,
        .nearest_filter = true);
    stage->stage_tex._descriptor = mel_gpu_pipeline_alloc_descriptor(&mel_sprite_pass()->pipeline, dev);
    mel_gpu_pipeline_write_texture(&mel_sprite_pass()->pipeline, dev,
        stage->stage_tex._descriptor, stage->stage_tex.image._view, stage->stage_tex._sampler);
    stage->stage_tex_handle = mel_texture_pool_register(mel_texture_pool(), &stage->stage_tex);
    stage->stage_loaded = true;
}

static void street_carlos_fight_stage_upload_hud(Street_Carlos_Fight_Stage* stage)
{
    if (!stage->fight_sff.atlas_pixels)
        return;

    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_texture_init(&stage->fight_tex, dev,
        .pixels = stage->fight_sff.atlas_pixels,
        .width = stage->fight_sff.atlas_width,
        .height = stage->fight_sff.atlas_height,
        .nearest_filter = true);
    stage->fight_tex._descriptor = mel_gpu_pipeline_alloc_descriptor(&mel_sprite_pass()->pipeline, dev);
    mel_gpu_pipeline_write_texture(&mel_sprite_pass()->pipeline, dev,
        stage->fight_tex._descriptor, stage->fight_tex.image._view, stage->fight_tex._sampler);
    stage->fight_tex_handle = mel_texture_pool_register(mel_texture_pool(), &stage->fight_tex);

    stage->hud = (Mugen_Hud){
        .fightdef = &stage->fightdef,
        .fight_sff = &stage->fight_sff,
        .fight_tex = stage->fight_tex_handle,
        .font = stage->ctx->body_font,
        .p1_mid_ratio = 1.0f,
        .p2_mid_ratio = 1.0f,
        .p1_power_mid = 0.0f,
        .p2_power_mid = 0.0f,
        .scale_x = (f32)GAME_W / (f32)stage->stage_def.localcoord_w,
        .scale_y = (f32)GAME_H / (f32)stage->stage_def.localcoord_h,
    };
    stage->hud_loaded = true;
}

static void fight_build_stage_def_job(i32 range_start, i32 range_end, i32 thread_index, void* user)
{
    MEL_UNUSED(range_start);
    MEL_UNUSED(range_end);
    MEL_UNUSED(thread_index);
    street_carlos_fight_stage_load_stage_def(user);
}

static void fight_build_stage_sff_job(i32 range_start, i32 range_end, i32 thread_index, void* user)
{
    MEL_UNUSED(range_start);
    MEL_UNUSED(range_end);
    MEL_UNUSED(thread_index);
    street_carlos_fight_stage_load_stage_sff(user);
}

static void fight_build_fightdef_job(i32 range_start, i32 range_end, i32 thread_index, void* user)
{
    MEL_UNUSED(range_start);
    MEL_UNUSED(range_end);
    MEL_UNUSED(thread_index);
    street_carlos_fight_stage_load_fightdef(user);
}

static void fight_build_hud_sff_job(i32 range_start, i32 range_end, i32 thread_index, void* user)
{
    MEL_UNUSED(range_start);
    MEL_UNUSED(range_end);
    MEL_UNUSED(thread_index);
    street_carlos_fight_stage_load_hud_sff(user);
}

static void street_carlos_fight_stage_build_sync(Street_Carlos_Fight_Stage* stage)
{
    fight_build_stage_def_job(0, 1, 0, stage);
    fight_build_stage_sff_job(0, 1, 0, stage);
    fight_build_fightdef_job(0, 1, 0, stage);
    fight_build_hud_sff_job(0, 1, 0, stage);
}

Mel_Progress_Status street_carlos_fight_stage_progress(void* user)
{
    Street_Carlos_Fight_Stage* stage = user;
    f32 progress = 0.0f;

    switch (stage->prep.phase)
    {
        case STREET_CARLOS_FIGHT_PREP_IDLE: progress = 0.0f; break;
        case STREET_CARLOS_FIGHT_PREP_WAIT_BUILD:
            progress = 0.6f;
            break;
        case STREET_CARLOS_FIGHT_PREP_UPLOAD_CHAR: progress = 0.6f; break;
        case STREET_CARLOS_FIGHT_PREP_UPLOAD_STAGE: progress = 0.75f; break;
        case STREET_CARLOS_FIGHT_PREP_UPLOAD_HUD: progress = 0.9f; break;
        case STREET_CARLOS_FIGHT_PREP_READY: progress = 1.0f; break;
        case STREET_CARLOS_FIGHT_PREP_FAILED: progress = stage->prep.progress; break;
    }

#if STREET_CARLOS_FIGHT_SIMULATED_LOAD_MS > 0
    u64 elapsed_ticks = SDL_GetTicks() - stage->prep_started_at_ticks;
    f32 simulated = mel_clampf((f32)elapsed_ticks / (f32)STREET_CARLOS_FIGHT_SIMULATED_LOAD_MS, 0.0f, 1.0f);
    progress = mel_minf(progress, simulated);
#endif

    return (Mel_Progress_Status){
        .value = progress,
        .failed = stage->prep.phase == STREET_CARLOS_FIGHT_PREP_FAILED,
    };
}

static void street_carlos_fight_stage_fail(Street_Carlos_Fight_Stage* stage, const char* message)
{
    SDL_Log("%s", message);
    stage->prep.phase = STREET_CARLOS_FIGHT_PREP_FAILED;
    street_carlos_fight_stage_release_assets(stage);
    mel_progress_clear(&stage->ctx->load_progress);
    street_carlos_show_main_menu(stage->ctx);
    mel_stage_detach(&stage->ctx->loading_stage->base.stage);
}

static void street_carlos_fight_stage_enter(Street_Carlos_Fight_Stage* stage)
{
    stage->match = mugen_match_create(
        .p1_char = stage->p1_char,
        .p2_char = stage->p2_char,
        .stage = &stage->stage_def,
        .screen_w = (f32)GAME_W,
        .alloc = mel_alloc_heap());
    mugen_match_start(stage->match);
    mel_register_sim(&stage->match->sim);
    street_carlos_fight_stage_init_input(stage);
    stage->show_inputs = true;
    stage->paused = false;
    stage->initialized = true;
    SDL_Log("FIGHT! P1: WASD + UIO(xyz) JKL(abc)  P2: Arrows + Numpad  Tab: hitboxes  T: tests  ESC: pause  `: console");
}

static void street_carlos_fight_stage_leave(Street_Carlos_Fight_Stage* stage)
{
    if (stage->initialized)
    {
        mel_input_stack_shutdown(&stage->input_stack);
        mel_input_bindings_shutdown(&stage->p1_bindings);
        mel_input_bindings_shutdown(&stage->p2_bindings);
        mel_unregister_sim(&stage->match->sim);
        mugen_match_end(stage->match);
        stage->match = NULL;
    }

    street_carlos_fight_stage_release_assets(stage);
    stage->show_hitboxes = false;
    stage->show_tests = false;
    stage->paused = false;
    stage->initialized = false;
    stage->prep = (Street_Carlos_Fight_Prep_State){0};
}

static void street_carlos_fight_stage_start(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    Street_Carlos_Fight_Stage* stage = user;
    mel_progress_clear(&stage->ctx->load_progress);

    if (stage->prep.phase != STREET_CARLOS_FIGHT_PREP_READY || !stage->p1_char || !stage->p2_char)
    {
        SDL_Log("Fight stage enabled before preparation was ready");
        street_carlos_show_main_menu(stage->ctx);
        mel_stage_detach(&stage->stage);
        return;
    }

    street_carlos_fight_stage_enter(stage);
}

static void street_carlos_fight_stage_end(Mel_Stage* base, void* user)
{
    MEL_UNUSED(base);
    street_carlos_fight_stage_leave(user);
}

void street_carlos_fight_stage_init(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx)
{
    *stage = (Street_Carlos_Fight_Stage){ .ctx = ctx };
    mel_stage_init(&stage->stage,
        .on_start = street_carlos_fight_stage_start,
        .on_end = street_carlos_fight_stage_end,
        .user = stage,
        .start_enabled = false);
}

void street_carlos_fight_stage_shutdown(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx)
{
    street_carlos_fight_stage_leave(stage);
    MEL_UNUSED(ctx);
    mel_stage_shutdown(&stage->stage);
}

void street_carlos_fight_stage_prepare(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx, Mugen_Char* p1_char, Mugen_Char* p2_char, str8 stage_path)
{
    street_carlos_fight_stage_leave(stage);
    stage->ctx = ctx;
    stage->p1_char = p1_char;
    stage->p2_char = p2_char;
    stage->selected_stage_path = stage_path;
    stage->prep = (Street_Carlos_Fight_Prep_State){
        .phase = STREET_CARLOS_FIGHT_PREP_WAIT_BUILD,
        .progress = 0.2f,
    };
    stage->prep_started_at_ticks = SDL_GetTicks();
    street_carlos_fight_stage_build_sync(stage);
}

void street_carlos_fight_stage_tick(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx)
{
    MEL_UNUSED(ctx);
    switch (stage->prep.phase)
    {
        case STREET_CARLOS_FIGHT_PREP_IDLE:
        case STREET_CARLOS_FIGHT_PREP_READY:
        case STREET_CARLOS_FIGHT_PREP_FAILED:
            return;

        case STREET_CARLOS_FIGHT_PREP_WAIT_BUILD:
        {
            stage->prep.progress = 0.6f;
            stage->prep.phase = STREET_CARLOS_FIGHT_PREP_UPLOAD_CHAR;
        } break;

        case STREET_CARLOS_FIGHT_PREP_UPLOAD_CHAR:
            street_carlos_fight_stage_upload_chars(stage);
            stage->prep.progress = 0.75f;
            stage->prep.phase = STREET_CARLOS_FIGHT_PREP_UPLOAD_STAGE;
            break;

        case STREET_CARLOS_FIGHT_PREP_UPLOAD_STAGE:
            street_carlos_fight_stage_upload_stage(stage);
            stage->prep.progress = 0.9f;
            stage->prep.phase = STREET_CARLOS_FIGHT_PREP_UPLOAD_HUD;
            break;

        case STREET_CARLOS_FIGHT_PREP_UPLOAD_HUD:
            street_carlos_fight_stage_upload_hud(stage);
            stage->prep.progress = 1.0f;
            stage->prep.phase = STREET_CARLOS_FIGHT_PREP_READY;
            break;
    }
}

bool street_carlos_fight_stage_handle_event(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx, SDL_Event* event)
{
    MEL_UNUSED(ctx);
    if (!mel_stage_is_enabled(&stage->stage))
        return false;

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_TAB && !event->key.repeat)
    {
        stage->show_hitboxes = !stage->show_hitboxes;
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_T && !event->key.repeat)
    {
        stage->show_tests = !stage->show_tests;
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_I && !event->key.repeat
        && (SDL_GetModState() & SDL_KMOD_LSHIFT))
    {
        stage->show_inputs = !stage->show_inputs;
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_M && !event->key.repeat
        && (SDL_GetModState() & SDL_KMOD_LSHIFT))
    {
        stage->show_moves = !stage->show_moves;
        return true;
    }
    mel_input_stack_dispatch(&stage->input_stack, event);
    return true;
}

bool street_carlos_fight_stage_is_live(Street_Carlos_Fight_Stage* stage)
{
    return stage && mel_stage_is_enabled(&stage->stage) && stage->match != NULL;
}

void street_carlos_fight_stage_set_paused(Street_Carlos_Fight_Stage* stage, bool paused)
{
    if (!street_carlos_fight_stage_is_live(stage))
        return;

    stage->paused = paused;
    if (paused)
    {
        mugen_match_set_inputs(stage->match, (Mugen_Player_Inputs){0}, (Mugen_Player_Inputs){0});
        mugen_match_pause(stage->match);
    }
    else
    {
        mugen_match_start(stage->match);
    }
}

void street_carlos_fight_stage_toggle_hitboxes(Street_Carlos_Fight_Stage* stage)
{
    if (stage)
        stage->show_hitboxes = !stage->show_hitboxes;
}

void street_carlos_fight_stage_toggle_tests(Street_Carlos_Fight_Stage* stage)
{
    if (stage)
        stage->show_tests = !stage->show_tests;
}

void street_carlos_fight_stage_draw_world(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list)
{
    MEL_UNUSED(ctx);
    if (!mel_stage_is_enabled(&stage->stage) || !stage->match)
        return;

    Fighter* p1 = mugen_match_p1(stage->match);
    Fighter* p2 = mugen_match_p2(stage->match);
    Mugen_Camera* cam = mugen_match_camera(stage->match);
    f32 sx = (f32)GAME_W / (f32)stage->stage_def.localcoord_w;
    f32 sy = (f32)GAME_H / (f32)stage->stage_def.localcoord_h;
    f32 zoff = stage->stage_def.zoffset;

    if (stage->stage_loaded)
        game_draw_stage_layer(&stage->stage_def, &stage->stage_sff, stage->stage_tex_handle, cam, 0, list);

    game_draw_afterimage(&p1->cns_state, stage->p1_char, stage->p1_char_tex_handle, cam, zoff, sx, sy, list);
    game_draw_afterimage(&p2->cns_state, stage->p2_char, stage->p2_char_tex_handle, cam, zoff, sx, sy, list);
    game_draw_fighter(p1, stage->p1_char, stage->p1_char_tex_handle, cam, zoff, sx, sy, list);
    game_draw_fighter(p2, stage->p2_char, stage->p2_char_tex_handle, cam, zoff, sx, sy, list);

    for (u32 i = 0; i < p1->helper_count; i++)
        game_draw_helper(&p1->helpers[i], stage->p1_char, stage->p1_char_tex_handle, cam, zoff, sx, sy, list);
    for (u32 i = 0; i < p2->helper_count; i++)
        game_draw_helper(&p2->helpers[i], stage->p2_char, stage->p2_char_tex_handle, cam, zoff, sx, sy, list);

    if (stage->stage_loaded)
        game_draw_stage_layer(&stage->stage_def, &stage->stage_sff, stage->stage_tex_handle, cam, 1, list);
}

void street_carlos_fight_stage_draw_hud(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list)
{
    MEL_UNUSED(ctx);
    if (!mel_stage_is_enabled(&stage->stage) || !stage->match || !stage->hud_loaded)
        return;

    Fighter* p1 = mugen_match_p1(stage->match);
    Fighter* p2 = mugen_match_p2(stage->match);
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
    mugen_hud_draw(&stage->hud, &hud_state, list);

    if (stage->show_inputs)
    {
        Mugen_Player_Inputs p1_inputs = mugen_match_get_player_inputs(stage->match, MUGEN_MATCH_PLAYER_1);
        Mugen_Player_Inputs p2_inputs = mugen_match_get_player_inputs(stage->match, MUGEN_MATCH_PLAYER_2);
        game_draw_input_display(p1_inputs, &p1->commands, p1->cns_state.stateno,
            ctx->body_font, 4.0f, 24.0f, list);
        game_draw_input_display(p2_inputs, &p2->commands, p2->cns_state.stateno,
            ctx->body_font, (f32)GAME_W - 80.0f, 24.0f, list);
    }

    if (stage->show_moves)
    {
        game_draw_move_list(&stage->p1_char->cmd, p1->cns_state.stateno,
            ctx->body_font, (f32)GAME_W * 0.5f - 60.0f, 24.0f, (f32)GAME_H - 30.0f, list);
    }
}

void street_carlos_fight_stage_draw_debug(Street_Carlos_Fight_Stage* stage, Street_Carlos_Ctx* ctx, Mel_Render_List* list)
{
    MEL_UNUSED(ctx);
    if (!mel_stage_is_enabled(&stage->stage) || !stage->match || !stage->show_hitboxes)
        return;

    Fighter* p1 = mugen_match_p1(stage->match);
    Fighter* p2 = mugen_match_p2(stage->match);
    Mugen_Camera* cam = mugen_match_camera(stage->match);
    f32 sx = (f32)GAME_W / (f32)stage->stage_def.localcoord_w;
    f32 sy = (f32)GAME_H / (f32)stage->stage_def.localcoord_h;
    f32 zoff = stage->stage_def.zoffset;

    game_draw_debug_boxes(p1, cam, zoff, sx, sy, list);
    game_draw_debug_boxes(p2, cam, zoff, sx, sy, list);

    for (u32 i = 0; i < p1->helper_count; i++)
        game_draw_helper_debug_boxes(&p1->helpers[i], cam, zoff, sx, sy, list);
    for (u32 i = 0; i < p2->helper_count; i++)
        game_draw_helper_debug_boxes(&p2->helpers[i], cam, zoff, sx, sy, list);
}

bool street_carlos_fight_stage_show_imgui(Street_Carlos_Fight_Stage* stage)
{
    if (!mel_stage_is_enabled(&stage->stage) || !stage->show_tests)
        return false;
    game_test_imgui();
    return true;
}
