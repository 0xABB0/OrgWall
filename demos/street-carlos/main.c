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
#include "render.graph.h"
#include "render.target.h"
#include "render.camera.h"
#include "render.list.h"
#include "render.pass.h"
#include "texture.pool.h"
#include "gpu.texture.h"
#include "gpu.buffer.h"
#include "gpu.pipeline.h"
#include "gpu.cmd.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "sim.ctx.h"
#include "input.stack.h"
#include "input.bindings.h"
#include "async.task.h"

#include "stage.h"
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

typedef struct {
    f32 x, y;
    f32 u, v;
    f32 r, g, b, a;
} Blit_Vertex;

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;

static Mel_Render_Target s_offscreen;
static Mel_Render_Target s_swapchain_target;
static Mel_Render_Graph s_graph;

static Mel_Camera s_game_camera;
static Mel_Render_List s_game_list;

static VkSampler s_nearest_sampler;
static VkDescriptorSet s_blit_descriptor;
static Mel_Gpu_Buffer s_blit_vbuf;
static Mel_Gpu_Buffer s_blit_ibuf;
static Mel_Mat4 s_blit_proj;

static Mel_Sim_Ctx s_task_sim;
static u8 s_task_event_buf[256];

static Mel_Input_Stack s_input_stack;
static Mel_Input_Bindings s_p1_bindings;
static Mel_Input_Bindings s_p2_bindings;

typedef struct {
    Mugen_Match* match;
    u32 player_index;
    Mugen_Player_Inputs inputs;
} Match_Input_User;

static Match_Input_User s_p1_input_user;
static Match_Input_User s_p2_input_user;

static Mugen_Match* s_match;
static bool s_show_hitboxes;
static bool s_show_tests;

static Mel_Io s_io;
static Mel_Vfs s_vfs;
static Mugen_Roster s_roster;
static Mugen_Char* s_fight_char;
static Mel_Gpu_Texture s_char_tex;
static Mel_Texture_Handle s_char_tex_handle;

static Mugen_Stage s_stage;
static Mugen_Sff s_stage_sff;
static Mel_Gpu_Texture s_stage_tex;
static Mel_Texture_Handle s_stage_tex_handle;
static bool s_stage_loaded;

static Mugen_Fightdef s_fightdef;
static Mugen_Sff s_fight_sff;
static Mel_Gpu_Texture s_fight_tex;
static Mel_Texture_Handle s_fight_tex_handle;
static Mugen_Hud s_hud;
static bool s_fight_hud_loaded;

static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Font_Handle s_title_font;
static Mel_Font_Handle s_body_font;

static u32 s_current_screen;
static Mel_Task_Ctx* s_task_ctx;
static Mel_Task_Handle s_load_task;
static bool s_fight_initialized;

static void update_blit_quad(void)
{
    u32 win_w = mel_render_target_width(&s_swapchain_target);
    u32 win_h = mel_render_target_height(&s_swapchain_target);

    f32 scale_x = (f32)win_w / (f32)GAME_W;
    f32 scale_y = (f32)win_h / (f32)GAME_H;
    f32 scale = scale_x < scale_y ? scale_x : scale_y;

    f32 scaled_w = (f32)GAME_W * scale;
    f32 scaled_h = (f32)GAME_H * scale;
    f32 ox = ((f32)win_w - scaled_w) * 0.5f;
    f32 oy = ((f32)win_h - scaled_h) * 0.5f;

    Blit_Vertex* v = (Blit_Vertex*)s_blit_vbuf.mapped;
    v[0] = (Blit_Vertex){ ox,            oy,            0, 0,  1, 1, 1, 1 };
    v[1] = (Blit_Vertex){ ox + scaled_w, oy,            1, 0,  1, 1, 1, 1 };
    v[2] = (Blit_Vertex){ ox + scaled_w, oy + scaled_h, 1, 1,  1, 1, 1, 1 };
    v[3] = (Blit_Vertex){ ox,            oy + scaled_h, 0, 1,  1, 1, 1, 1 };

    s_blit_proj = mel_mat4_ortho(0, (f32)win_w, 0, (f32)win_h, -1, 1);
}

static void blit_pass(Mel_Render_Pass_Ctx* ctx)
{
    update_blit_quad();
    mel_gpu_buffer_flush(&s_blit_vbuf, ctx->cmd.dev);

    Mel_Gpu_Pipeline* pip = &mel_sprite_pass()->pipeline;

    mel_gpu_pipeline_bind(pip, ctx->cmd.cmd);

    vkCmdPushConstants(ctx->cmd.cmd, pip->layout,
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mel_Mat4), &s_blit_proj);

    vkCmdBindDescriptorSets(ctx->cmd.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pip->layout, 0, 1, &s_blit_descriptor, 0, nullptr);

    VkBuffer vbufs[] = { s_blit_vbuf.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(ctx->cmd.cmd, 0, 1, vbufs, offsets);
    vkCmdBindIndexBuffer(ctx->cmd.cmd, s_blit_ibuf.buffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdDrawIndexed(ctx->cmd.cmd, 6, 1, 0, 0, 0);
}

static void imgui_pass(Mel_Render_Pass_Ctx* ctx)
{
    MEL_UNUSED(ctx);

    if (s_current_screen == SCREEN_FIGHT && s_show_tests)
        game_test_imgui();

    igRender();
    ImDrawData* draw_data = igGetDrawData();
    if (draw_data && draw_data->CmdListsCount > 0)
        ImGui_ImplVulkan_RenderDrawData(draw_data, ctx->cmd.cmd, VK_NULL_HANDLE);
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

static void init_input(void)
{
    mel_input_bindings_init(&s_p1_bindings);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_A, ACT_MOVE_LEFT);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_D, ACT_MOVE_RIGHT);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_S, ACT_CROUCH);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_W, ACT_JUMP);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_U, ACT_BTN_X);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_I, ACT_BTN_Y);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_O, ACT_BTN_Z);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_J, ACT_BTN_A);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_K, ACT_BTN_B);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_L, ACT_BTN_C);

    mel_input_bindings_init(&s_p2_bindings);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_LEFT, ACT_MOVE_LEFT);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_RIGHT, ACT_MOVE_RIGHT);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_DOWN, ACT_CROUCH);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_UP, ACT_JUMP);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_KP_7, ACT_BTN_X);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_KP_8, ACT_BTN_Y);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_KP_9, ACT_BTN_Z);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_KP_4, ACT_BTN_A);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_KP_5, ACT_BTN_B);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_KP_6, ACT_BTN_C);

    s_p1_input_user = (Match_Input_User){
        .match = s_match,
        .player_index = MUGEN_MATCH_PLAYER_1,
    };
    s_p2_input_user = (Match_Input_User){
        .match = s_match,
        .player_index = MUGEN_MATCH_PLAYER_2,
    };
    mugen_match_set_inputs(s_match, s_p1_input_user.inputs, s_p2_input_user.inputs);

    mel_input_stack_init(&s_input_stack);

    mel_input_stack_push(&s_input_stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &s_p1_bindings,
        .on_action = match_input_on_action,
        .user = &s_p1_input_user);

    mel_input_stack_push(&s_input_stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &s_p2_bindings,
        .on_action = match_input_on_action,
        .user = &s_p2_input_user);
}

static void load_stage(void)
{
    str8 stage_data = mel_vfs_read_text_alloc(&s_vfs, S8("/stages/kfm.def"), mel_alloc_heap());
    if (stage_data.len == 0)
    {
        SDL_Log("Failed to read stage def, using defaults");
        mugen_stage_load(&s_stage, (str8){0}, mel_alloc_heap());
        return;
    }

    mugen_stage_load(&s_stage, stage_data, mel_alloc_heap());
    mel_dealloc(mel_alloc_heap(), stage_data.data);

    if (s_stage.spr_path.len > 0)
    {
        str8 spr_full = str8_fmt_alloc(mel_alloc_heap(), "/stages/%.*s",
            (int)s_stage.spr_path.len, (char*)s_stage.spr_path.data);

        if (mugen_sff_load(&s_stage_sff, &s_vfs, spr_full, mel_alloc_heap()))
        {
            Mel_Gpu_Device* dev = mel_gpu_dev();
            mel_gpu_texture_init(&s_stage_tex, dev,
                .pixels = s_stage_sff.atlas_pixels,
                .width = s_stage_sff.atlas_width,
                .height = s_stage_sff.atlas_height,
                .nearest_filter = true);
            s_stage_tex.descriptor = mel_gpu_pipeline_alloc_descriptor(&mel_sprite_pass()->pipeline, dev);
            mel_gpu_pipeline_write_texture(&mel_sprite_pass()->pipeline, dev,
                s_stage_tex.descriptor, s_stage_tex.image.view, s_stage_tex.sampler);
            s_stage_tex_handle = mel_texture_pool_register(mel_texture_pool(), &s_stage_tex);
            s_stage_loaded = true;
            SDL_Log("Stage loaded: %d BG layers, spr=%.*s",
                s_stage.bg_count, (int)s_stage.spr_path.len, (char*)s_stage.spr_path.data);
        }
        else
        {
            SDL_Log("Failed to load stage SFF: %.*s", (int)spr_full.len, (char*)spr_full.data);
        }

        mel_dealloc(mel_alloc_heap(), spr_full.data);
    }
}

static void screen_enter_fight(Mugen_Char* ch)
{
    s_fight_char = ch;

    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_texture_init(&s_char_tex, dev,
        .pixels = ch->sff.atlas_pixels,
        .width = ch->sff.atlas_width,
        .height = ch->sff.atlas_height,
        .nearest_filter = true);
    s_char_tex.descriptor = mel_gpu_pipeline_alloc_descriptor(&mel_sprite_pass()->pipeline, dev);
    mel_gpu_pipeline_write_texture(&mel_sprite_pass()->pipeline, dev,
        s_char_tex.descriptor, s_char_tex.image.view, s_char_tex.sampler);
    s_char_tex_handle = mel_texture_pool_register(mel_texture_pool(), &s_char_tex);

    load_stage();

    {
        str8 fdef_data = mel_vfs_read_text_alloc(&s_vfs, S8("/data/fight.def"), mel_alloc_heap());
        if (fdef_data.len > 0)
        {
            mugen_fightdef_load(&s_fightdef, fdef_data, mel_alloc_heap());
            mel_dealloc(mel_alloc_heap(), fdef_data.data);

            if (s_fightdef.files.sff.len > 0)
            {
                str8 sff_path = str8_fmt_alloc(mel_alloc_heap(), "/data/%.*s",
                    (int)s_fightdef.files.sff.len, (char*)s_fightdef.files.sff.data);

                if (mugen_sff_load(&s_fight_sff, &s_vfs, sff_path, mel_alloc_heap()))
                {
                    Mel_Gpu_Device* fight_dev = mel_gpu_dev();
                    mel_gpu_texture_init(&s_fight_tex, fight_dev,
                        .pixels = s_fight_sff.atlas_pixels,
                        .width = s_fight_sff.atlas_width,
                        .height = s_fight_sff.atlas_height,
                        .nearest_filter = true);
                    s_fight_tex.descriptor = mel_gpu_pipeline_alloc_descriptor(&mel_sprite_pass()->pipeline, fight_dev);
                    mel_gpu_pipeline_write_texture(&mel_sprite_pass()->pipeline, fight_dev,
                        s_fight_tex.descriptor, s_fight_tex.image.view, s_fight_tex.sampler);
                    s_fight_tex_handle = mel_texture_pool_register(mel_texture_pool(), &s_fight_tex);

                    s_hud = (Mugen_Hud){
                        .fightdef = &s_fightdef,
                        .fight_sff = &s_fight_sff,
                        .fight_tex = s_fight_tex_handle,
                        .font_pool = &s_font_pool,
                        .font = s_body_font,
                        .p1_mid_ratio = 1.0f,
                        .p2_mid_ratio = 1.0f,
                        .p1_power_mid = 0.0f,
                        .p2_power_mid = 0.0f,
                        .scale_x = (f32)GAME_W / (f32)s_stage.localcoord_w,
                        .scale_y = (f32)GAME_H / (f32)s_stage.localcoord_h,
                    };
                    s_fight_hud_loaded = true;
                    SDL_Log("Fight HUD loaded: sff=%.*s",
                        (int)s_fightdef.files.sff.len, (char*)s_fightdef.files.sff.data);
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

    s_match = mugen_match_create(
        .p1_char = ch,
        .p2_char = ch,
        .stage = &s_stage,
        .screen_w = (f32)GAME_W,
        .alloc = mel_alloc_heap());

    mugen_match_start(s_match);
    mel_register_sim(&s_match->sim);

    init_input();
    s_fight_initialized = true;
    s_current_screen = SCREEN_FIGHT;

    SDL_Log("FIGHT! P1: WASD + UIO(xyz) JKL(abc)  P2: Arrows + Numpad  Tab: hitboxes  T: tests  ESC: quit");
}

static void on_roster_loaded(Mel_Task_Handle handle, u32 status, void* user)
{
    MEL_UNUSED(user);

    if (status == MEL_TASK_STATUS_DONE)
    {
        Mugen_Char* ch = mugen_roster_find(&s_roster, S8("kfm"));
        if (ch)
            screen_enter_fight(ch);
        else
            SDL_Log("Character 'kfm' not found in roster!");
    }
    else
    {
        SDL_Log("Roster loading failed!");
    }

    mel_task_release(s_task_ctx, handle);
    s_load_task = MEL_TASK_HANDLE_NULL;
}

static void screen_start_loading(void)
{
    s_current_screen = SCREEN_LOADING;

    s_load_task = mugen_roster_load(&s_roster,
        .folder_path = S8("/chars/"),
        .on_complete = on_roster_loaded);
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
    if (mel_slotmap_handle_valid(s_load_task))
        progress = mel_task_progress(s_task_ctx, s_load_task);

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

static void produce_game_list(Mel_Render_List* list, void* user)
{
    MEL_UNUSED(user);

    switch (s_current_screen) {
    case SCREEN_TITLE:
        draw_title(list);
        break;

    case SCREEN_LOADING:
        draw_loading(list);
        break;

    case SCREEN_FIGHT:
    {
        Fighter* p1 = mugen_match_p1(s_match);
        Fighter* p2 = mugen_match_p2(s_match);
        Mugen_Camera* cam = mugen_match_camera(s_match);
        f32 sx = (f32)GAME_W / (f32)s_stage.localcoord_w;
        f32 sy = (f32)GAME_H / (f32)s_stage.localcoord_h;
        f32 zoff = s_stage.zoffset;

        if (s_stage_loaded)
            game_draw_stage_layer(&s_stage, &s_stage_sff, s_stage_tex_handle, cam, 0, list);

        game_draw_fighter(p1, s_fight_char, s_char_tex_handle, cam, zoff, sx, sy, list);
        game_draw_fighter(p2, s_fight_char, s_char_tex_handle, cam, zoff, sx, sy, list);

        for (u32 i = 0; i < p1->helper_count; i++)
            game_draw_helper(&p1->helpers[i], s_fight_char, s_char_tex_handle, cam, zoff, sx, sy, list);
        for (u32 i = 0; i < p2->helper_count; i++)
            game_draw_helper(&p2->helpers[i], s_fight_char, s_char_tex_handle, cam, zoff, sx, sy, list);

        if (s_stage_loaded)
            game_draw_stage_layer(&s_stage, &s_stage_sff, s_stage_tex_handle, cam, 1, list);

        if (s_fight_hud_loaded)
        {
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
            mugen_hud_draw(&s_hud, &hud_state, list);
        }

        if (s_show_hitboxes)
        {
            game_draw_debug_boxes(p1, cam, zoff, sx, sy, list);
            game_draw_debug_boxes(p2, cam, zoff, sx, sy, list);

            for (u32 i = 0; i < p1->helper_count; i++)
                game_draw_helper_debug_boxes(&p1->helpers[i], cam, zoff, sx, sy, list);
            for (u32 i = 0; i < p2->helper_count; i++)
                game_draw_helper_debug_boxes(&p2->helpers[i], cam, zoff, sx, sy, list);
        }
        break;
    }
    }
}

static void task_sim_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);
    mel_task_tick(s_task_ctx);
}

static void init_render(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    mel_render_list_init(&s_game_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL,
        .alloc = mel_alloc_heap());
    mel_render_list_add_producer(&s_game_list, produce_game_list, NULL);

    mel_render_target_init(&s_offscreen, dev,
        .name = S8("game_fb"),
        .width = GAME_W,
        .height = GAME_H,
        .format = sc->format);

    mel_render_target_init_swapchain(&s_swapchain_target, sc, dev, S8("backbuffer"));

    s_game_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)GAME_W, 0, (f32)GAME_H, -1, 1),
    };

    mel_gpu_buffer_init(&s_blit_vbuf, dev,
        .size = 4 * sizeof(Blit_Vertex),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);

    mel_gpu_buffer_init(&s_blit_ibuf, dev,
        .size = 6 * sizeof(u16),
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);

    u16* idx = (u16*)s_blit_ibuf.mapped;
    idx[0] = 0; idx[1] = 1; idx[2] = 2;
    idx[3] = 2; idx[4] = 3; idx[5] = 0;
    mel_gpu_buffer_flush(&s_blit_ibuf, dev);

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    vkCreateSampler(dev->device, &sampler_info, nullptr, &s_nearest_sampler);

    s_blit_descriptor = mel_gpu_pipeline_alloc_descriptor(&mel_sprite_pass()->pipeline, dev);
    mel_gpu_pipeline_write_texture(&mel_sprite_pass()->pipeline, dev,
        s_blit_descriptor, s_offscreen.image.view, s_nearest_sampler);

    mel_render_graph_init(&s_graph, .dev = dev, .alloc = mel_alloc_heap());

    u32 game_pass_id = mel_render_graph_add_pass(&s_graph, S8("game"),
        .fn = mel_sprite_pass_execute,
        .user = mel_sprite_pass(),
        .camera = &s_game_camera,
        .read_lists = MEL_LISTS(&s_game_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_offscreen, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.05f, .g = 0.05f, .b = 0.08f, .a = 1.0f } }));

    u32 blit_pass_id = mel_render_graph_add_pass(&s_graph, S8("blit"),
        .fn = blit_pass,
        .read_targets = MEL_TARGETS(&s_offscreen),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f } }));

    u32 imgui_pass_id = mel_render_graph_add_pass(&s_graph, S8("imgui"),
        .fn = imgui_pass,
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_LOAD }));

    mel_render_graph_pass_depends_on(&s_graph, blit_pass_id, game_pass_id);
    mel_render_graph_pass_depends_on(&s_graph, imgui_pass_id, blit_pass_id);
    mel_render_graph_compile(&s_graph);
    mel_set_render_graph(&s_graph);
}

static void on_init(void)
{
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

    Mel_Task_Ctx_Desc task_desc = {
        .alloc = mel_alloc_heap(),
        .io    = &s_io,
        .vfs   = &s_vfs,
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

    s_current_screen = SCREEN_TITLE;
    s_load_task = MEL_TASK_HANDLE_NULL;

    SDL_Log("Street Carlos - Title Screen");
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

    if (s_fight_initialized)
    {
        mel_input_stack_shutdown(&s_input_stack);
        mel_input_bindings_shutdown(&s_p1_bindings);
        mel_input_bindings_shutdown(&s_p2_bindings);
        mel_unregister_sim(&s_match->sim);
        mugen_match_end(s_match);
        s_match = NULL;
    }

    mel_unregister_sim(&s_task_sim);
    mel_sim_shutdown(&s_task_sim);

    Mel_Gpu_Device* dev = mel_gpu_dev();

    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_offscreen);
    mel_render_target_shutdown(&s_swapchain_target);

    if (s_fight_char)
        mel_gpu_texture_shutdown(&s_char_tex, dev);

    if (s_stage_loaded)
    {
        mel_gpu_texture_shutdown(&s_stage_tex, dev);
        mugen_sff_shutdown(&s_stage_sff, mel_alloc_heap());
    }
    mugen_stage_shutdown(&s_stage, mel_alloc_heap());

    if (s_fight_hud_loaded)
    {
        mel_gpu_texture_shutdown(&s_fight_tex, dev);
        mugen_sff_shutdown(&s_fight_sff, mel_alloc_heap());
    }
    mugen_fightdef_shutdown(&s_fightdef, mel_alloc_heap());

    mugen_roster_shutdown(&s_roster);

    mel_gpu_buffer_shutdown(&s_blit_vbuf, dev);
    mel_gpu_buffer_shutdown(&s_blit_ibuf, dev);
    vkDestroySampler(dev->device, s_nearest_sampler, nullptr);

    mel_render_list_shutdown(&s_game_list);

    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_vfs_unmount(&s_vfs, S8("/"));
    mel_vfs_unmount(&s_vfs, S8("/data"));
    mel_vfs_unmount(&s_vfs, S8("/stages"));
    mel_vfs_unmount(&s_vfs, S8("/fonts"));
    mel_task_ctx_destroy(s_task_ctx);
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
            s_show_hitboxes = !s_show_hitboxes;
            return;
        }
        if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_T && !event->key.repeat)
        {
            s_show_tests = !s_show_tests;
            return;
        }
        mel_input_stack_dispatch(&s_input_stack, event);
        break;
    }
}

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_event = app_event
)
