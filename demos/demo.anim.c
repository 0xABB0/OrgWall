#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <volk.h>

#include "core.app.h"
#include "core.engine.h"
#include "string.str8.h"
#include "gpu.texture.h"
#include "sprite.pass.h"
#include "render.list.h"
#include "render.graph.h"
#include "render.target.h"
#include "render.camera.h"
#include "texture.pool.h"
#include "font.atlas.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "anim.state.h"
#include "anim.sprite.h"
#include "anim.clip.h"
#include "allocator.heap.h"

#define WIDTH  640
#define HEIGHT 480

#define SPRITE_SRC_SIZE 32
#define SPRITE_SCALE    3
#define SPRITE_SIZE     (SPRITE_SRC_SIZE * SPRITE_SCALE)

#define STATE_IDLE 0
#define STATE_WALK 1
#define STATE_RUN  2
#define STATE_HIT  3
#define STATE_COUNT 4

#define COND_WALK 1
#define COND_RUN  2
#define COND_IDLE 3
#define COND_HIT  4

#define IDLE_FRAME_COUNT 1
#define WALK_FRAME_COUNT 4
#define RUN_FRAME_COUNT  4
#define HIT_FRAME_COUNT  3

typedef struct {
    Mel_Anim_State_Player player;
    Mel_Anim_State_Machine machine;
    Mel_Anim_State_Def state_defs[STATE_COUNT];
    Mel_Anim_Clip clips[STATE_COUNT];
    Mel_Anim_Mixer mixer;
    Mel_Anim_Transition idle_trans[3];
    Mel_Anim_Transition walk_trans[3];
    Mel_Anim_Transition run_trans[3];
    Mel_Anim_Transition hit_trans[1];
    const Mel_Alloc* alloc;
    u32 prev_state;
} AnimDemo;

static SDL_Window* s_window;
static Mel_Sprite_Pass* s_sp;
static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Io s_demo_io;
static Mel_Vfs s_demo_vfs;
static Mel_Vfs_Backend* s_fonts_backend;
static Mel_Font_Handle s_font_handle;

static Mel_Gpu_Texture s_idle_tex;
static Mel_Gpu_Texture s_walk_tex;
static Mel_Gpu_Texture s_run_tex;
static Mel_Gpu_Texture s_hit_tex;
static Mel_Texture_Handle s_idle_handle;
static Mel_Texture_Handle s_walk_handle;
static Mel_Texture_Handle s_run_handle;
static Mel_Texture_Handle s_hit_handle;
static AnimDemo s_demo;
static Mel_Render_Target s_swapchain_target;
static Mel_Render_Graph s_graph;
static Mel_Camera s_camera;
static Mel_Render_List s_sprite_list;
static Mel_Render_List s_font_list;

static const u32 s_frame_counts[STATE_COUNT] = {
    IDLE_FRAME_COUNT,
    WALK_FRAME_COUNT,
    RUN_FRAME_COUNT,
    HIT_FRAME_COUNT,
};

static const char* s_state_names[STATE_COUNT] = {
    "IDLE",
    "WALK",
    "RUN",
    "HIT",
};

static Mel_Texture_Handle init_texture(Mel_Gpu_Texture* tex, Mel_Engine* e, str8 path)
{
    mel_gpu_texture_init(tex, &e->dev, .path = path, .nearest_filter = true);
    tex->descriptor = mel_gpu_pipeline_alloc_descriptor(&s_sp->pipeline, &e->dev);
    mel_gpu_pipeline_write_texture(&s_sp->pipeline, &e->dev, tex->descriptor,
        tex->image.view, tex->sampler);
    return mel_texture_pool_register(e->texture_pool, tex);
}

static Mel_Texture_Handle texture_handle_for_state(u32 state)
{
    switch (state)
    {
        case STATE_IDLE: return s_idle_handle;
        case STATE_WALK: return s_walk_handle;
        case STATE_RUN:  return s_run_handle;
        case STATE_HIT:  return s_hit_handle;
        default:         return s_idle_handle;
    }
}

static u32 anim_demo_mixer_state(AnimDemo* d)
{
    Mel_Anim_Layer* layer = mel_anim_mixer_layer(&d->mixer, 0);
    if (!layer->clip) return STATE_IDLE;
    u32 state = (u32)layer->clip->name_hash;
    return state < STATE_COUNT ? state : STATE_IDLE;
}

static u32 anim_demo_frame_index(AnimDemo* d, u32 state)
{
    const Mel_Anim_Mixer_Output* out =
        mel_anim_mixer_find_output(&d->mixer, MEL_ANIM_PROP_SPRITE_FRAME);
    u32 frame = out ? (u32)out->value[0] : 0;
    u32 max_frame = s_frame_counts[state];
    return frame < max_frame ? frame : 0;
}

static void clear_all_conditions(Mel_Anim_State_Player* player)
{
    mel_anim_state_player_set_condition(player, COND_WALK, false);
    mel_anim_state_player_set_condition(player, COND_RUN, false);
    mel_anim_state_player_set_condition(player, COND_IDLE, false);
    mel_anim_state_player_set_condition(player, COND_HIT, false);
}

static void anim_demo_init(AnimDemo* d, const Mel_Alloc* alloc)
{
    memset(d, 0, sizeof(*d));
    d->alloc = alloc;

    u32 idle_frames[] = {0};
    f32 idle_durs[]   = {1.0f};
    d->clips[STATE_IDLE] = mel_anim_sprite_clip(alloc, STATE_IDLE,
        idle_frames, idle_durs, IDLE_FRAME_COUNT, true);

    u32 walk_frames[] = {0, 1, 2, 3};
    f32 walk_durs[]   = {0.15f, 0.15f, 0.15f, 0.15f};
    d->clips[STATE_WALK] = mel_anim_sprite_clip(alloc, STATE_WALK,
        walk_frames, walk_durs, WALK_FRAME_COUNT, true);

    u32 run_frames[] = {0, 1, 2, 3};
    f32 run_durs[]   = {0.10f, 0.10f, 0.10f, 0.10f};
    d->clips[STATE_RUN] = mel_anim_sprite_clip(alloc, STATE_RUN,
        run_frames, run_durs, RUN_FRAME_COUNT, true);

    u32 hit_frames[] = {0, 1, 2};
    f32 hit_durs[]   = {0.12f, 0.12f, 0.12f};
    d->clips[STATE_HIT] = mel_anim_sprite_clip(alloc, STATE_HIT,
        hit_frames, hit_durs, HIT_FRAME_COUNT, false);

    d->idle_trans[0] = (Mel_Anim_Transition){
        .target_state = STATE_WALK,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_WALK,
    };
    d->idle_trans[1] = (Mel_Anim_Transition){
        .target_state = STATE_RUN,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_RUN,
    };
    d->idle_trans[2] = (Mel_Anim_Transition){
        .target_state = STATE_HIT,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_HIT,
    };

    d->walk_trans[0] = (Mel_Anim_Transition){
        .target_state = STATE_RUN,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_RUN,
    };
    d->walk_trans[1] = (Mel_Anim_Transition){
        .target_state = STATE_IDLE,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_IDLE,
    };
    d->walk_trans[2] = (Mel_Anim_Transition){
        .target_state = STATE_HIT,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_HIT,
    };

    d->run_trans[0] = (Mel_Anim_Transition){
        .target_state = STATE_IDLE,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_IDLE,
    };
    d->run_trans[1] = (Mel_Anim_Transition){
        .target_state = STATE_WALK,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_WALK,
    };
    d->run_trans[2] = (Mel_Anim_Transition){
        .target_state = STATE_HIT,
        .mode = MEL_ANIM_TRANSITION_IMMEDIATE,
        .condition_hash = COND_HIT,
    };

    d->hit_trans[0] = (Mel_Anim_Transition){
        .target_state = STATE_IDLE,
        .mode = MEL_ANIM_TRANSITION_AT_END,
        .condition_hash = COND_HIT,
    };

    d->state_defs[STATE_IDLE] = (Mel_Anim_State_Def){
        .name_hash = STATE_IDLE,
        .clip = &d->clips[STATE_IDLE],
        .transitions = d->idle_trans,
        .transition_count = 3,
    };

    d->state_defs[STATE_WALK] = (Mel_Anim_State_Def){
        .name_hash = STATE_WALK,
        .clip = &d->clips[STATE_WALK],
        .transitions = d->walk_trans,
        .transition_count = 3,
    };

    d->state_defs[STATE_RUN] = (Mel_Anim_State_Def){
        .name_hash = STATE_RUN,
        .clip = &d->clips[STATE_RUN],
        .transitions = d->run_trans,
        .transition_count = 3,
    };

    d->state_defs[STATE_HIT] = (Mel_Anim_State_Def){
        .name_hash = STATE_HIT,
        .clip = &d->clips[STATE_HIT],
        .transitions = d->hit_trans,
        .transition_count = 1,
    };

    mel_anim_mixer_init(&d->mixer, alloc);
    mel_anim_mixer_add_layer(&d->mixer);
    mel_anim_state_machine_init(&d->machine, d->state_defs, STATE_COUNT, STATE_IDLE);
    mel_anim_state_player_init(&d->player, alloc,
        .machine = &d->machine, .mixer = &d->mixer, .mixer_layer = 0);

    d->prev_state = STATE_IDLE;
}

static void anim_demo_destroy(AnimDemo* d)
{
    mel_anim_state_player_destroy(&d->player);
    mel_anim_mixer_destroy(&d->mixer);
    for (u32 i = 0; i < STATE_COUNT; i++)
        mel_anim_clip_destroy(&d->clips[i], d->alloc);
}

static void draw_hero(Mel_Render_List* list, AnimDemo* d, f32 cx, f32 cy)
{
    u32 state = anim_demo_mixer_state(d);
    u32 frame = anim_demo_frame_index(d, state);
    u32 frame_count = s_frame_counts[state];
    Mel_Texture_Handle tex = texture_handle_for_state(state);

    f32 u0 = (f32)frame / (f32)frame_count;
    f32 u1 = (f32)(frame + 1) / (f32)frame_count;

    Mel_Sprite_Entry* e = mel_render_list_push(list,
        mel_sort_key_sprite(0, 0.0f, 0, mel_texture_bucket(tex)));
    *e = (Mel_Sprite_Entry){
        .pos = mel_vec2(cx - SPRITE_SIZE / 2.0f, cy - SPRITE_SIZE / 2.0f),
        .size = mel_vec2((f32)SPRITE_SIZE, (f32)SPRITE_SIZE),
        .uv = mel_rect(u0, 0.0f, u1 - u0, 1.0f),
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .tex = tex,
    };
}

static void draw_info(Mel_Render_List* list, Mel_Font_Atlas_Pool* pool, Mel_Font_Handle font, AnimDemo* d, f32 cx, f32 y)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.5f, 0.5f, 0.5f, 1.0f);
    Mel_Vec4 green = mel_vec4(0.3f, 0.9f, 0.3f, 1.0f);

    char buf[128];
    u32 state = anim_demo_mixer_state(d);
    u32 frame = anim_demo_frame_index(d, state);

    snprintf(buf, sizeof(buf), "State: %s", s_state_names[state]);
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), cx - 60.0f, y, green);

    snprintf(buf, sizeof(buf), "Frame: %u / %u", frame, s_frame_counts[state]);
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), cx - 60.0f, y + 24.0f, white);

    f32 controls_y = y + 64.0f;
    mel_font_atlas_draw_text(pool, font, list, S8("[W] Walk  [R] Run"), cx - 80.0f, controls_y, dim);
    mel_font_atlas_draw_text(pool, font, list, S8("[I] Idle  [H] Hit"), cx - 80.0f, controls_y + 22.0f, dim);
    mel_font_atlas_draw_text(pool, font, list, S8("[ESC] Quit"), cx - 80.0f, controls_y + 44.0f, dim);

    f32 diagram_y = controls_y + 84.0f;
    Mel_Vec4 cyan = mel_vec4(0.3f, 0.8f, 0.9f, 1.0f);
    mel_font_atlas_draw_text(pool, font, list, S8("IDLE --W--> WALK --R--> RUN"), cx - 140.0f, diagram_y, cyan);
    mel_font_atlas_draw_text(pool, font, list, S8("  ^           |           |"), cx - 140.0f, diagram_y + 18.0f, cyan);
    mel_font_atlas_draw_text(pool, font, list, S8("  +----I------+-----I-----+"), cx - 140.0f, diagram_y + 36.0f, cyan);
    mel_font_atlas_draw_text(pool, font, list, S8("  ^                        "), cx - 140.0f, diagram_y + 54.0f, cyan);
    mel_font_atlas_draw_text(pool, font, list, S8("  +------HIT<---H----------"), cx - 140.0f, diagram_y + 72.0f, cyan);
}

static void on_init(Mel_Engine* e)
{
    Mel_Gpu_Device* dev = &e->dev;
    s_sp = e->sprite_pass;

    mel_font_atlas_pool_init(&s_font_pool, &e->allocator, dev, &s_demo_vfs, .texture_pool = e->texture_pool);
    s_font_handle = mel_font_atlas_pool_load(&s_font_pool,
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 18.0f);


    s_idle_handle = init_texture(&s_idle_tex, e, S8("assets/hero/idle_DOWN.png"));
    s_walk_handle = init_texture(&s_walk_tex, e, S8("assets/hero/walk_DOWN.png"));
    s_run_handle  = init_texture(&s_run_tex, e, S8("assets/hero/run_DOWN.png"));
    s_hit_handle  = init_texture(&s_hit_tex, e, S8("assets/hero/hit_DOWN.png"));

    anim_demo_init(&s_demo, mel_alloc_heap());

    mel_render_list_init(&s_sprite_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_list_init(&s_font_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_target_init_swapchain(&s_swapchain_target, &e->swapchain, &e->dev, S8("backbuffer"));

    s_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)e->swapchain.extent.width,
                                      0, (f32)e->swapchain.extent.height, -1, 1),
    };

    mel_render_graph_init(&s_graph, .dev = &e->dev, .alloc = mel_alloc_heap());
    mel_render_graph_add_pass(&s_graph, S8("sprite"),
        .fn = mel_sprite_pass_execute,
        .user = s_sp,
        .camera = &s_camera,
        .read_lists = MEL_LISTS(&s_sprite_list, &s_font_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.08f, .g = 0.08f, .b = 0.1f, .a = 1.0f } }));
    mel_render_graph_compile(&s_graph);
    e->render_graph = &s_graph;

    SDL_Log("Anim demo ready! W=Walk, R=Run, I=Idle, H=Hit, ESC=Quit");
}

static void app_init(Mel_App* app)
{
    s_window = SDL_CreateWindow("Melody Animation Showcase", WIDTH, HEIGHT,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    assert(s_window);

    mel_engine_init(&app->engine,
        .window = s_window,
        .app_name = S8("Melody Animation Showcase"),
        .enable_validation = true,
        .enable_imgui = false);

    Mel_Io_Desc io_desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&s_demo_io, &io_desc);
    Mel_Vfs_Desc vfs_desc = { .allocator = mel_alloc_heap(), .io = &s_demo_io };
    mel_vfs_init(&s_demo_vfs, &vfs_desc);
    s_fonts_backend = mel_vfs_backend_os_create(mel_alloc_heap(), S8("/"));
    mel_vfs_mount(&s_demo_vfs, S8("/"), s_fonts_backend, 0, false);

    on_init(&app->engine);
}

static void app_shutdown(Mel_App* app)
{
    Mel_Gpu_Device* dev = &app->engine.dev;
    mel_gpu_device_wait_idle(dev);

    mel_render_list_shutdown(&s_sprite_list);
    mel_render_list_shutdown(&s_font_list);
    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_swapchain_target);

    anim_demo_destroy(&s_demo);

    mel_gpu_texture_shutdown(&s_idle_tex, dev);
    mel_gpu_texture_shutdown(&s_walk_tex, dev);
    mel_gpu_texture_shutdown(&s_run_tex, dev);
    mel_gpu_texture_shutdown(&s_hit_tex, dev);

    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_vfs_unmount(&s_demo_vfs, S8("/"));
    mel_vfs_shutdown(&s_demo_vfs);
    mel_io_shutdown(&s_demo_io);
    mel_vfs_backend_os_destroy(s_fonts_backend);
}

static void app_update(Mel_App* app, f32 dt)
{
    AnimDemo* d = &s_demo;
    d->prev_state = d->player.current_state;
    mel_anim_state_player_update(&d->player);

    if (d->prev_state == STATE_HIT && d->player.current_state == STATE_IDLE)
    {
        clear_all_conditions(&d->player);
    }

    mel_anim_mixer_update(&d->mixer, dt);

    f32 cx = (f32)app->engine.swapchain.extent.width / 2.0f;
    f32 hero_y = (f32)app->engine.swapchain.extent.height * 0.33f;

    mel_render_list_clear(&s_sprite_list);
    draw_hero(&s_sprite_list, d, cx, hero_y);

    mel_render_list_clear(&s_font_list);
    draw_info(&s_font_list, &s_font_pool, s_font_handle, d, cx, hero_y + SPRITE_SIZE / 2.0f + 20.0f);
}

static void app_event(Mel_App* app, SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        app->should_quit = true;
        return;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        AnimDemo* d = &s_demo;

        switch (event->key.scancode)
        {
            case SDL_SCANCODE_W:
                clear_all_conditions(&d->player);
                mel_anim_state_player_set_condition(&d->player, COND_WALK, true);
                break;
            case SDL_SCANCODE_R:
                clear_all_conditions(&d->player);
                mel_anim_state_player_set_condition(&d->player, COND_RUN, true);
                break;
            case SDL_SCANCODE_I:
                clear_all_conditions(&d->player);
                mel_anim_state_player_set_condition(&d->player, COND_IDLE, true);
                break;
            case SDL_SCANCODE_H:
                clear_all_conditions(&d->player);
                mel_anim_state_player_set_condition(&d->player, COND_HIT, true);
                break;
            default: break;
        }
    }
}

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_update = app_update,
    .on_event = app_event
)
