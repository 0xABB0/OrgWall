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
#include "allocator.heap.h"
#include "sim.ctx.h"
#include "input.stack.h"
#include "input.bindings.h"

#include "fighter.h"
#include "combat.h"
#include "carlos.h"
#include "game_test.h"
#include "mugen_sff.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "async.io.h"
#include "gpu.texture.h"
#include <string.h>

#define GAME_W 384
#define GAME_H 224

#define STAGE_FLOOR_Y 176.0f
#define STAGE_LEFT 20.0f
#define STAGE_RIGHT (GAME_W - 20.0f)

typedef struct {
    f32 x, y;
    f32 u, v;
    f32 r, g, b, a;
} Blit_Vertex;

enum {
    ACT_MOVE_LEFT = 1,
    ACT_MOVE_RIGHT,
    ACT_CROUCH,
    ACT_JUMP,
    ACT_PUNCH,
};

static SDL_Window* s_window;
static Mel_Engine* s_engine;

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
static Mugen_Sff s_poison_sff;
static Mel_Gpu_Texture s_poison_tex;
static Mel_Texture_Handle s_poison_handle;
static bool s_poison_loaded;

static f32 game_to_screen_y(f32 game_y, f32 h)
{
    return STAGE_FLOOR_Y - game_y - h;
}

static void draw_box_outline(Fighter_Box box, Mel_Vec4 color, f32 thickness)
{
    f32 sy = game_to_screen_y(box.y, box.h);

    mel_draw_sprite(&s_game_list, .pos = mel_vec2(box.x, sy),
        .size = mel_vec2(box.w, thickness), .color = color);
    mel_draw_sprite(&s_game_list, .pos = mel_vec2(box.x, sy + box.h - thickness),
        .size = mel_vec2(box.w, thickness), .color = color);
    mel_draw_sprite(&s_game_list, .pos = mel_vec2(box.x, sy),
        .size = mel_vec2(thickness, box.h), .color = color);
    mel_draw_sprite(&s_game_list, .pos = mel_vec2(box.x + box.w - thickness, sy),
        .size = mel_vec2(thickness, box.h), .color = color);
}

static void draw_mugen_sprite(Fighter* f, Mugen_Sff* sff, Mel_Texture_Handle tex, u16 group, u16 number)
{
    u32 frame_idx = mugen_sff_find_frame(sff, group, number);
    Mel_SpriteFrame* frame = mel_spritesheet_get_frame(&sff->sheet, frame_idx);
    if (!frame) return;

    f32 u0, v0, u1, v1;
    mel_spritesheet_get_frame_uv(&sff->sheet, frame_idx, &u0, &v0, &u1, &v1);

    f32 w = (f32)frame->width;
    f32 h = (f32)frame->height;

    f32 feet_x = f->x + f->character->width * 0.5f;
    f32 feet_y = STAGE_FLOOR_Y - f->y;

    f32 draw_x = feet_x - (f32)frame->offset_x;
    f32 draw_y = feet_y - (f32)frame->offset_y;

    if (!f->facing_right)
    {
        f32 tmp = u0;
        u0 = u1;
        u1 = tmp;
        draw_x = feet_x - (w - (f32)frame->offset_x);
    }

    static bool logged = false;
    if (!logged)
    {
        SDL_Log("MUGEN draw: pos(%.1f, %.1f) size(%.0f, %.0f) uv(%.4f, %.4f, %.4f, %.4f) offset(%d,%d) feet(%.1f, %.1f)",
                draw_x, draw_y, w, h, u0, v0, u1 - u0, v1 - v0,
                frame->offset_x, frame->offset_y, feet_x, feet_y);
        logged = true;
    }

    mel_draw_sprite(&s_game_list,
        .pos = mel_vec2(draw_x, draw_y),
        .size = mel_vec2(w, h),
        .color = mel_vec4(1, 1, 1, 1),
        .tex = tex,
        .uv = mel_rect(u0, v0, u1 - u0, v1 - v0));
}

static void draw_fighter(Fighter* f, Mel_Vec4 base_color, bool use_mugen)
{
    if (use_mugen && s_poison_loaded)
    {
        draw_mugen_sprite(f, &s_poison_sff, s_poison_handle, 0, 0);
    }
    else
    {
        Character_Def* c = f->character;
        f32 w = c->width;
        f32 h = c->height;
        Mel_Vec4 color = base_color;

        if (f->locomotion == LOCO_CROUCH && !f->current_move)
        {
            h = c->crouch_height;
            w = c->width * 1.1f;
        }
        else if (f->locomotion == LOCO_HITSTUN)
        {
            color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }

        if (f->current_move)
        {
            if (f->move_phase == MOVE_PHASE_STARTUP)
                color = mel_vec4(color.x * 0.7f, color.y * 0.7f, color.z * 0.7f, 1.0f);
            else if (f->move_phase == MOVE_PHASE_ACTIVE)
                color = mel_vec4(1.0f, 1.0f, 0.3f, 1.0f);
            else if (f->move_phase == MOVE_PHASE_RECOVERY)
                color = mel_vec4(color.x * 0.5f, color.y * 0.5f, color.z * 0.5f, 1.0f);
        }

        f32 draw_y = game_to_screen_y(f->y, h);

        mel_draw_sprite(&s_game_list,
            .pos = mel_vec2(f->x, draw_y),
            .size = mel_vec2(w, h),
            .color = color);
    }

    Mel_Vec4 attack_color = use_mugen ? mel_vec4(1.0f, 1.0f, 0.3f, 0.5f) : base_color;

    if (f->current_move && f->move_phase == MOVE_PHASE_ACTIVE)
    {
        Move_Def* m = f->current_move;
        if (m->hit_w > 0 && m->hit_h > 0 && !m->spawns_projectile)
        {
            Fighter_Box hb = fighter_hitbox(f);
            f32 fist_y = game_to_screen_y(hb.y, hb.h);
            mel_draw_sprite(&s_game_list,
                .pos = mel_vec2(hb.x, fist_y),
                .size = mel_vec2(hb.w, hb.h),
                .color = attack_color);
        }
    }

    for (u32 i = 0; i < MAX_PROJECTILES; i++)
    {
        if (!f->projectiles[i].active) continue;
        Projectile* p = &f->projectiles[i];
        f32 proj_y = game_to_screen_y(p->y, p->hit_h);
        mel_draw_sprite(&s_game_list,
            .pos = mel_vec2(p->x, proj_y),
            .size = mel_vec2(p->hit_w, p->hit_h),
            .color = mel_vec4(1.0f, 0.8f, 0.2f, 1.0f));
    }
}

static void draw_debug_boxes(Fighter* f)
{
    Fighter_Box hurt = fighter_hurtbox(f);
    draw_box_outline(hurt, mel_vec4(0.0f, 1.0f, 0.0f, 1.0f), 1.0f);

    if (fighter_has_active_hitbox(f))
    {
        Fighter_Box hit = fighter_hitbox(f);
        draw_box_outline(hit, mel_vec4(1.0f, 0.0f, 0.0f, 1.0f), 1.0f);
    }

    for (u32 i = 0; i < MAX_PROJECTILES; i++)
    {
        Projectile* p = &f->projectiles[i];
        if (!p->active) continue;
        Fighter_Box pb = { p->x, p->y, p->hit_w, p->hit_h };
        draw_box_outline(pb, mel_vec4(1.0f, 1.0f, 0.0f, 1.0f), 1.0f);
    }
}

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

    Mel_Gpu_Pipeline* pip = &s_engine->sprite_pass->pipeline;

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
    return action >= ACT_MOVE_LEFT && action <= ACT_PUNCH;
}

static void game_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

static void on_init(Mel_Engine* e)
{
    Mel_Gpu_Device* dev = &e->dev;

    s_engine = e;

    mel_render_list_init(&s_game_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_target_init(&s_offscreen, dev,
        .name = S8("game_fb"),
        .width = GAME_W,
        .height = GAME_H,
        .format = e->swapchain.format);

    mel_render_target_init_swapchain(&s_swapchain_target, &e->swapchain, dev, S8("backbuffer"));

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

    s_blit_descriptor = mel_gpu_pipeline_alloc_descriptor(&e->sprite_pass->pipeline, dev);
    mel_gpu_pipeline_write_texture(&e->sprite_pass->pipeline, dev,
        s_blit_descriptor, s_offscreen.image.view, s_nearest_sampler);

    mel_render_graph_init(&s_graph, .dev = dev, .alloc = mel_alloc_heap());

    u32 game_pass_id = mel_render_graph_add_pass(&s_graph, S8("game"),
        .fn = mel_sprite_pass_execute,
        .user = e->sprite_pass,
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
    e->render_graph = &s_graph;

    mel_io_init(&s_io, &(Mel_Io_Desc){ .allocator = mel_alloc_heap(), .worker_count = 0 });
    mel_vfs_init(&s_vfs, &(Mel_Vfs_Desc){ .allocator = mel_alloc_heap(), .io = &s_io });
    Mel_Vfs_Backend* os_be = mel_vfs_backend_os_create(mel_alloc_heap(), S8("demos/street-carlos"));
    mel_vfs_mount(&s_vfs, S8("/"), os_be, 0, false);

    s_poison_loaded = mugen_sff_load(&s_poison_sff, &s_vfs, S8("/chars/poi-son/poi-son.sff"), mel_alloc_heap());
    if (s_poison_loaded)
    {
        mel_gpu_texture_init(&s_poison_tex, dev,
            .pixels = s_poison_sff.atlas_pixels,
            .width = s_poison_sff.atlas_width,
            .height = s_poison_sff.atlas_height,
            .nearest_filter = true);
        s_poison_tex.descriptor = mel_gpu_pipeline_alloc_descriptor(&e->sprite_pass->pipeline, dev);
        mel_gpu_pipeline_write_texture(&e->sprite_pass->pipeline, dev,
            s_poison_tex.descriptor, s_poison_tex.image.view, s_poison_tex.sampler);
        s_poison_handle = mel_texture_pool_register(e->texture_pool, &s_poison_tex);
    }

    fighter_init(&s_p1, &CARLOS_DEF, CARLOS_MOVES, CARLOS_MOVE_COUNT, 80.0f, true, mel_alloc_heap());
    fighter_init(&s_p2, &CARLOS_DEF, CARLOS_MOVES, CARLOS_MOVE_COUNT,
                 GAME_W - 80.0f - CARLOS_DEF.width, false, mel_alloc_heap());

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));

    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&s_sim, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(fixed, game_update);

    mel_engine_register_sim(e, &s_sim);

    mel_input_bindings_init(&s_p1_bindings);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_A, ACT_MOVE_LEFT);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_D, ACT_MOVE_RIGHT);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_S, ACT_CROUCH);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_W, ACT_JUMP);
    mel_input_bindings_add(&s_p1_bindings, SDL_SCANCODE_J, ACT_PUNCH);

    mel_input_bindings_init(&s_p2_bindings);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_LEFT, ACT_MOVE_LEFT);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_RIGHT, ACT_MOVE_RIGHT);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_DOWN, ACT_CROUCH);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_UP, ACT_JUMP);
    mel_input_bindings_add(&s_p2_bindings, SDL_SCANCODE_KP_1, ACT_PUNCH);

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

    SDL_Log("Street Carlos ready! P1: WASD + J(punch)  P2: Arrows + Numpad1(punch)  Tab: hitboxes  T: tests  ESC: quit");
}

static void game_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    fighter_tick(&s_p1, dt, STAGE_LEFT, STAGE_RIGHT);
    fighter_tick(&s_p2, dt, STAGE_LEFT, STAGE_RIGHT);

    combat_check_hits(&s_p1, &s_p2);
    combat_check_hits(&s_p2, &s_p1);
    combat_check_projectiles(&s_p1, &s_p2);
    combat_check_projectiles(&s_p2, &s_p1);

    mel_render_list_clear(&s_game_list);

    mel_draw_sprite(&s_game_list,
        .pos = mel_vec2(0, STAGE_FLOOR_Y),
        .size = mel_vec2(GAME_W, GAME_H - STAGE_FLOOR_Y),
        .color = mel_vec4(0.25f, 0.18f, 0.12f, 1.0f));

    mel_draw_sprite(&s_game_list,
        .pos = mel_vec2(0, STAGE_FLOOR_Y - 2),
        .size = mel_vec2(GAME_W, 2),
        .color = mel_vec4(0.5f, 0.35f, 0.2f, 1.0f));

    draw_fighter(&s_p1, mel_vec4(0.2f, 0.4f, 0.9f, 1.0f), true);
    draw_fighter(&s_p2, mel_vec4(0.9f, 0.2f, 0.2f, 1.0f), false);

    if (s_show_hitboxes)
    {
        draw_debug_boxes(&s_p1);
        draw_debug_boxes(&s_p2);
    }
}

static bool has_arg(Mel_App* app, const char* arg)
{
    for (int i = 1; i < app->argc; i++)
        if (strcmp(app->argv[i], arg) == 0) return true;
    return false;
}

static void app_init(Mel_App* app)
{
    if (has_arg(app, "--test"))
    {
        int result = mel_test_main(app->argc, app->argv);
        app->should_quit = true;
        exit(result);
    }

    s_window = SDL_CreateWindow("Street Carlos", GAME_W * 3, GAME_H * 3,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!s_window)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return;
    }

    mel_engine_init(&app->engine,
        .window = s_window,
        .app_name = S8("Street Carlos"),
        .enable_validation = true,
        .enable_imgui = true);

    on_init(&app->engine);
}

static void app_shutdown(Mel_App* app)
{
    mel_input_stack_shutdown(&s_input_stack);
    mel_input_bindings_shutdown(&s_p1_bindings);
    mel_input_bindings_shutdown(&s_p2_bindings);

    mel_engine_unregister_sim(&app->engine, &s_sim);
    mel_sim_shutdown(&s_sim);

    Mel_Gpu_Device* dev = &app->engine.dev;
    mel_gpu_device_wait_idle(dev);

    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_offscreen);
    mel_render_target_shutdown(&s_swapchain_target);

    if (s_poison_loaded)
    {
        mel_gpu_texture_shutdown(&s_poison_tex, dev);
        mugen_sff_shutdown(&s_poison_sff, mel_alloc_heap());
    }

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
