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

#include "stage.h"
#include "actions.h"
#include "fighter.h"
#include "mugen_char.h"
#include "game_draw.h"
#include "game_test.h"
#include "combat.h"
#include "command.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "async.io.h"
#include <string.h>

typedef struct {
    f32 x, y;
    f32 u, v;
    f32 r, g, b, a;
} Blit_Vertex;

static SDL_Window* s_window;
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

static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];

static Mel_Input_Stack s_input_stack;
static Mel_Input_Bindings s_p1_bindings;
static Mel_Input_Bindings s_p2_bindings;

static Fighter s_p1;
static Fighter s_p2;
static bool s_show_hitboxes;
static bool s_show_tests;

static Mel_Io s_io;
static Mel_Vfs s_vfs;
static Mugen_Char s_mugen_char;

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

    if (s_show_tests)
        game_test_imgui();

    igRender();
    ImDrawData* draw_data = igGetDrawData();
    if (draw_data && draw_data->CmdListsCount > 0)
        ImGui_ImplVulkan_RenderDrawData(draw_data, ctx->cmd.cmd, VK_NULL_HANDLE);
}

static bool fighter_on_action(Mel_Input_Action action, f32 value, void* user)
{
    Fighter* f = (Fighter*)user;
    fighter_on_input(f, action, value);
    return action >= ACT_MOVE_LEFT && action <= ACT_BTN_Z;
}

static void game_fixed_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    fighter_tick(&s_p1, dt, STAGE_LEFT, STAGE_RIGHT);
    fighter_tick(&s_p2, dt, STAGE_LEFT, STAGE_RIGHT);

    combat_resolve(&s_p1, &s_p2);
    fighter_apply_combat_state(&s_p1);
    fighter_apply_combat_state(&s_p2);
}

static void game_draw(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);

    mel_render_list_clear(&s_game_list);

    game_draw_stage(&s_game_list);

    game_draw_fighter(&s_p1, &s_mugen_char, &s_game_list);
    game_draw_fighter(&s_p2, &s_mugen_char, &s_game_list);

    if (s_show_hitboxes)
    {
        game_draw_debug_boxes(&s_p1, &s_game_list);
        game_draw_debug_boxes(&s_p2, &s_game_list);
    }
}

static void init_render(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    mel_render_list_init(&s_game_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

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

    mel_input_stack_init(&s_input_stack);

    mel_input_stack_push(&s_input_stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &s_p1_bindings,
        .on_action = fighter_on_action,
        .user = &s_p1);

    mel_input_stack_push(&s_input_stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &s_p2_bindings,
        .on_action = fighter_on_action,
        .user = &s_p2);
}

static void on_init(void)
{
    init_render();

    mel_io_init(&s_io, &(Mel_Io_Desc){ .allocator = mel_alloc_heap(), .worker_count = 0 });
    mel_vfs_init(&s_vfs, &(Mel_Vfs_Desc){ .allocator = mel_alloc_heap(), .io = &s_io });
    Mel_Vfs_Backend* os_be = mel_vfs_backend_os_create(mel_alloc_heap(), S8("demos/street-carlos"));
    mel_vfs_mount(&s_vfs, S8("/"), os_be, 0, false);

    mugen_char_load(&s_mugen_char,
        .dev = mel_gpu_dev(),
        .sprite_pass = mel_sprite_pass(),
        .tex_pool = mel_texture_pool(),
        .vfs = &s_vfs,
        .sff_path = S8("/chars/kfm/kfm.sff"),
        .air_path = S8("/chars/kfm/kfm.air"),
        .cmd_path = S8("/chars/kfm/kfm.cmd"),
        .cns_path = S8("/chars/kfm/kfm.cns"),
        .common_cns_path = S8("/chars/kfm/common1.cns"),
        .alloc = mel_alloc_heap());

    fighter_init(&s_p1, mel_alloc_heap(),
        .start_x = 80.0f,
        .facing_right = true,
        .clip_pool = &s_mugen_char.clip_pool,
        .action_map = s_mugen_char.action_map,
        .action_map_count = s_mugen_char.action_map_count);

    fighter_init(&s_p2, mel_alloc_heap(),
        .start_x = GAME_W - 80.0f,
        .facing_right = false,
        .clip_pool = &s_mugen_char.clip_pool,
        .action_map = s_mugen_char.action_map,
        .action_map_count = s_mugen_char.action_map_count);

    for (u32 i = 0; i < s_mugen_char.cmd.command_count; i++)
    {
        Mugen_Cmd_Def* c = &s_mugen_char.cmd.commands[i];
        command_list_add(&s_p1.commands, c->name, c->command, .time = c->time, .buf_time = 1);
        command_list_add(&s_p2.commands, c->name, c->command, .time = c->time, .buf_time = 1);
    }

    if (s_mugen_char.cns_loaded)
    {
        fighter_enable_cns(&s_p1, &s_mugen_char.cns, &s_mugen_char.common_cns, &s_mugen_char.cmd_cns);
        fighter_enable_cns(&s_p2, &s_mugen_char.cns, &s_mugen_char.common_cns, &s_mugen_char.cmd_cns);
    }

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));

    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&s_sim, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(fixed, game_fixed_update);

    mel_sim_add_variable(&s_sim, game_draw);

    mel_register_sim(&s_sim);

    init_input();

    SDL_Log("Street Carlos ready! P1: WASD + UIO(xyz) JKL(abc)  P2: Arrows + Numpad  Tab: hitboxes  T: tests  ESC: quit");
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
    s_window = mel_window_get(s_window_handle)->sdl;
    s_swapchain_handle = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_window_handle);
    mel_imgui_init(s_window, &mel_swapchain_registry_get(s_swapchain_handle)->swapchain);

    on_init();
}

static void app_shutdown(Mel_App* app)
{
    mel_input_stack_shutdown(&s_input_stack);
    mel_input_bindings_shutdown(&s_p1_bindings);
    mel_input_bindings_shutdown(&s_p2_bindings);

    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    Mel_Gpu_Device* dev = mel_gpu_dev();

    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_offscreen);
    mel_render_target_shutdown(&s_swapchain_target);

    mel_anim_player_destroy(&s_p1.player);
    mel_anim_player_destroy(&s_p2.player);
    mugen_char_shutdown(&s_mugen_char, dev, mel_alloc_heap());

    mel_gpu_buffer_shutdown(&s_blit_vbuf, dev);
    mel_gpu_buffer_shutdown(&s_blit_ibuf, dev);
    vkDestroySampler(dev->device, s_nearest_sampler, nullptr);

    mel_render_list_shutdown(&s_game_list);

    SDL_Log("Street Carlos shutdown");
}

static void app_event(Mel_App* app, SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        app->should_quit = true;
        return;
    }

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
}

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_event = app_event
)
