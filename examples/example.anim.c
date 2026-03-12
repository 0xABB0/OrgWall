#include <SDL3/SDL.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <volk.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
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
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "sprite.sheet.h"
#include "anim.sprite.h"
#include "anim.clip.h"
#include "anim.registry.h"
#include "anim.player.h"
#include "anim.pose.h"
#include "anim.pipeline.h"
#include "allocator.heap.h"
#include "collection.slotmap.h"
#include "hash.xxh.h"
#include "sim.ctx.h"

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

#define IDLE_FRAME_COUNT 1
#define WALK_FRAME_COUNT 4
#define RUN_FRAME_COUNT  4
#define HIT_FRAME_COUNT  3

typedef struct {
    Mel_Anim_Clip_Pool clip_pool;
    Mel_Anim_Clip_Handle clip_handles[STATE_COUNT];
    Mel_Sprite_Sheet sheets[STATE_COUNT];
    u64 frame_prop;
    Mel_Anim_Player player;
    u32 current_state;
    const Mel_Alloc* alloc;
} AnimDemo;

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Sprite_Pass* s_sp;
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
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];

static const char* s_state_names[STATE_COUNT] = {
    "IDLE",
    "WALK",
    "RUN",
    "HIT",
};

static Mel_Texture_Handle init_texture(Mel_Gpu_Texture* tex, str8 path)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_texture_init(tex, dev, .path = path, .nearest_filter = true);
    tex->descriptor = mel_gpu_pipeline_alloc_descriptor(&s_sp->pipeline, dev);
    mel_gpu_pipeline_write_texture(&s_sp->pipeline, dev, tex->descriptor,
        tex->image.view, tex->sampler);
    return mel_texture_pool_register(mel_texture_pool(), tex);
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

static void anim_demo_init(AnimDemo* d, const Mel_Alloc* alloc)
{
    memset(d, 0, sizeof(*d));
    d->alloc = alloc;

    mel_anim_registry_init(alloc);
    mel_slotmap_init(&d->clip_pool, alloc, .item_size = sizeof(Mel_Anim_Clip));

    d->frame_prop = mel_xxh3_64("frame", 5);

    struct {
        const char* name;
        u32 frame_count;
        f32 frame_dur;
        bool looping;
    } clip_defs[STATE_COUNT] = {
        { "idle", IDLE_FRAME_COUNT, 1.0f,  true  },
        { "walk", WALK_FRAME_COUNT, 0.15f, true  },
        { "run",  RUN_FRAME_COUNT,  0.10f, true  },
        { "hit",  HIT_FRAME_COUNT,  0.12f, false },
    };

    for (u32 s = 0; s < STATE_COUNT; s++)
    {
        mel_sprite_sheet_init(&d->sheets[s], alloc);
        mel_sprite_sheet_from_grid(&d->sheets[s],
            .cols = clip_defs[s].frame_count, .rows = 1);

        u64 name_hash = mel_xxh3_64(clip_defs[s].name, (u32)strlen(clip_defs[s].name));
        Mel_Sprite_Anim_Def def;
        mel_sprite_anim_def_init(&def, name_hash, d->frame_prop, alloc);
        def.is_looping = clip_defs[s].looping;

        for (u32 f = 0; f < clip_defs[s].frame_count; f++)
            mel_sprite_anim_def_push_frame(&def, f, clip_defs[s].frame_dur);

        Mel_Anim_Clip clip = mel_sprite_anim_def_compile(&def, alloc);
        d->clip_handles[s] = mel_slotmap_insert(&d->clip_pool, &clip);
        mel_sprite_anim_def_destroy(&def);
    }

    mel_anim_player_init(&d->player, alloc, &d->clip_pool);
    mel_anim_player_play(&d->player, d->clip_handles[STATE_IDLE]);
    d->current_state = STATE_IDLE;
}

static void anim_demo_destroy(AnimDemo* d)
{
    mel_anim_player_destroy(&d->player);

    for (u32 s = 0; s < STATE_COUNT; s++)
    {
        Mel_Anim_Clip* clip = (Mel_Anim_Clip*)mel_slotmap_get(&d->clip_pool, d->clip_handles[s]);
        mel_anim_clip_destroy(clip, d->alloc);
        mel_sprite_sheet_destroy(&d->sheets[s]);
    }
    mel_slotmap_free(&d->clip_pool);
}

static void anim_demo_switch_state(AnimDemo* d, u32 new_state)
{
    if (new_state == d->current_state) return;
    d->current_state = new_state;
    mel_anim_player_play(&d->player, d->clip_handles[new_state], .crossfade = 0.05f);
}

static u32 anim_demo_frame_index(AnimDemo* d)
{
    f32 frame_f;
    mel_anim_player_get_float(&d->player, d->frame_prop, d->alloc, &frame_f);
    u32 frame = (u32)frame_f;
    u32 max_frame = d->sheets[d->current_state].frame_count;
    return frame < max_frame ? frame : 0;
}

static void draw_hero(Mel_Render_List* list, AnimDemo* d, f32 cx, f32 cy)
{
    u32 state = d->current_state;
    u32 frame = anim_demo_frame_index(d);
    Mel_Texture_Handle tex = texture_handle_for_state(state);
    Mel_Rect uv = mel_sprite_sheet_frame(&d->sheets[state], frame);

    Mel_Sprite_Entry* e = mel_render_list_push(list,
        mel_sort_key_sprite(0, 0.0f, 0, mel_texture_bucket(tex)));
    *e = (Mel_Sprite_Entry){
        .pos = mel_vec2(cx - SPRITE_SIZE / 2.0f, cy - SPRITE_SIZE / 2.0f),
        .size = mel_vec2((f32)SPRITE_SIZE, (f32)SPRITE_SIZE),
        .uv = uv,
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
    u32 state = d->current_state;
    u32 frame = anim_demo_frame_index(d);

    snprintf(buf, sizeof(buf), "State: %s", s_state_names[state]);
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), cx - 60.0f, y, green);

    snprintf(buf, sizeof(buf), "Frame: %u / %u", frame, d->sheets[state].frame_count);
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

static void on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    s_sp = mel_sprite_pass();

    s_font_handle = mel_font_atlas_pool_load(mel_font_pool(),
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 18.0f);


    s_idle_handle = init_texture(&s_idle_tex, S8("assets/hero/idle_DOWN.png"));
    s_walk_handle = init_texture(&s_walk_tex, S8("assets/hero/walk_DOWN.png"));
    s_run_handle  = init_texture(&s_run_tex, S8("assets/hero/run_DOWN.png"));
    s_hit_handle  = init_texture(&s_hit_tex, S8("assets/hero/hit_DOWN.png"));

    anim_demo_init(&s_demo, mel_alloc_heap());

    mel_render_list_init(&s_sprite_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_list_init(&s_font_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_target_init_swapchain(&s_swapchain_target, sc, dev, S8("backbuffer"));

    s_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)sc->extent.width,
                                      0, (f32)sc->extent.height, -1, 1),
    };

    mel_render_graph_init(&s_graph, .dev = dev, .alloc = mel_alloc_heap());
    mel_render_graph_add_pass(&s_graph, S8("sprite"),
        .fn = mel_sprite_pass_execute,
        .user = s_sp,
        .camera = &s_camera,
        .read_lists = MEL_LISTS(&s_sprite_list, &s_font_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.08f, .g = 0.08f, .b = 0.1f, .a = 1.0f } }));
    mel_render_graph_compile(&s_graph);
    mel_set_render_graph(&s_graph);

    SDL_Log("Anim demo ready! W=Walk, R=Run, I=Idle, H=Hit, ESC=Quit");
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

void app_init(void)
{
    mel_init(.app_name = S8("Melody Animation Showcase"), .enable_validation = true);
    s_window_handle = mel_window_create(S8("Melody Animation Showcase"), .width = WIDTH, .height = HEIGHT);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_window_handle);
    mel_vfs_mount_native(mel_vfs(), S8("/"), S8("/"), 0, false);

    on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, app_update);
    mel_register_sim(&s_sim);
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    Mel_Gpu_Device* dev = mel_gpu_dev();

    mel_render_list_shutdown(&s_sprite_list);
    mel_render_list_shutdown(&s_font_list);
    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_swapchain_target);

    anim_demo_destroy(&s_demo);

    mel_gpu_texture_shutdown(&s_idle_tex, dev);
    mel_gpu_texture_shutdown(&s_walk_tex, dev);
    mel_gpu_texture_shutdown(&s_run_tex, dev);
    mel_gpu_texture_shutdown(&s_hit_tex, dev);

    mel_vfs_unmount(mel_vfs(), S8("/"));
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    AnimDemo* d = &s_demo;

    mel_anim_player_update(&d->player, dt);

    if (d->current_state == STATE_HIT && d->player.phase >= 1.0f)
        anim_demo_switch_state(d, STATE_IDLE);

    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    f32 cx = (f32)sc->extent.width / 2.0f;
    f32 hero_y = (f32)sc->extent.height * 0.33f;

    mel_render_list_clear(&s_sprite_list);
    draw_hero(&s_sprite_list, d, cx, hero_y);

    mel_render_list_clear(&s_font_list);
    draw_info(&s_font_list, mel_font_pool(), s_font_handle, d, cx, hero_y + SPRITE_SIZE / 2.0f + 20.0f);
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        mel_quit();
        return;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        AnimDemo* d = &s_demo;

        switch (event->key.scancode)
        {
            case SDL_SCANCODE_W:
                anim_demo_switch_state(d, STATE_WALK);
                break;
            case SDL_SCANCODE_R:
                anim_demo_switch_state(d, STATE_RUN);
                break;
            case SDL_SCANCODE_I:
                anim_demo_switch_state(d, STATE_IDLE);
                break;
            case SDL_SCANCODE_H:
                anim_demo_switch_state(d, STATE_HIT);
                break;
            default: break;
        }
    }
}
