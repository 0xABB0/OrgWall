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
#include "sprite.pass.h"
#include "render.graph.h"
#include "render.target.h"
#include "render.camera.h"
#include "render.list.h"
#include "render.source.h"
#include "render.view.h"
#include "render.frame_recipe.h"
#include "render.frame_plan.h"
#include "render.technique.h"
#include "texture.pool.h"
#include "font.atlas.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.vec2.h"
#include "math.geo.rect.h"
#include "anim.sprite.h"
#include "anim.clip.h"
#include "anim.registry.h"
#include "anim.pipeline.h"
#include "anim.pose.h"
#include "allocator.heap.h"
#include "sim.ctx.h"

#include <math.h>

#define WIDTH 640
#define HEIGHT 480

#define COLS 10
#define ROWS 5

#define BRICK_W 52.0f
#define BRICK_H 20.0f
#define BRICK_GAP 2.0f
#define BRICK_AREA_TOP 50.0f
#define BRICK_AREA_X ((WIDTH - (COLS * (BRICK_W + BRICK_GAP) - BRICK_GAP)) / 2.0f)

#define PADDLE_W 80.0f
#define PADDLE_H 12.0f
#define PADDLE_Y (HEIGHT - 30.0f - PADDLE_H)

#define BALL_SIZE 8.0f
#define BALL_INITIAL_SPEED 300.0f

#define BRICK_ALIVE 0
#define BRICK_DYING 1
#define BRICK_DEAD  2

#define DEATH_FRAME_COUNT 5

typedef struct {
    u8 state;
    u8 row;
    f32 anim_time;
    bool anim_finished;
} Brick;

typedef struct {
    Brick bricks[ROWS * COLS];
    f32 paddle_x;
    f32 ball_x, ball_y, ball_dx, ball_dy, ball_speed;
    bool ball_launched;
    i32 lives;
    u32 score, level;
    i32 bricks_alive;
    bool game_over;
    Mel_Anim_Clip death_clip;
    u64 death_prop;
    const Mel_Alloc* alloc;
    u32 rng_state;
    bool use_mouse;
} Breakout;

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Font_Handle s_font_handle;
static Breakout s_breakout;
static Mel_Render_Graph s_graph;
static Mel_Camera s_camera;
static Mel_Render_List s_sprite_list;
static Mel_Render_List s_font_list;
static Mel_Source_Handle s_sprite_source;
static Mel_Source_Handle s_font_source;
static Mel_View_Handle s_main_view;
static Mel_Frame_Recipe_Handle s_frame_recipe;
static Mel_Frame_Plan_Handle s_frame_plan;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];

static const Mel_Vec4 ROW_COLORS[ROWS] = {
    { .x = 0.9f, .y = 0.2f, .z = 0.2f, .w = 1.0f },
    { .x = 0.9f, .y = 0.6f, .z = 0.1f, .w = 1.0f },
    { .x = 0.9f, .y = 0.9f, .z = 0.2f, .w = 1.0f },
    { .x = 0.2f, .y = 0.9f, .z = 0.2f, .w = 1.0f },
    { .x = 0.2f, .y = 0.9f, .z = 0.9f, .w = 1.0f },
};

static u32 breakout_rand(Breakout* g)
{
    g->rng_state ^= g->rng_state << 13;
    g->rng_state ^= g->rng_state >> 17;
    g->rng_state ^= g->rng_state << 5;
    return g->rng_state;
}

static f32 brick_x(i32 col)
{
    return BRICK_AREA_X + (f32)col * (BRICK_W + BRICK_GAP);
}

static f32 brick_y(i32 row)
{
    return BRICK_AREA_TOP + (f32)row * (BRICK_H + BRICK_GAP);
}

static void breakout_reset_bricks(Breakout* g)
{
    g->bricks_alive = ROWS * COLS;
    for (i32 r = 0; r < ROWS; r++)
    {
        for (i32 c = 0; c < COLS; c++)
        {
            i32 idx = r * COLS + c;
            g->bricks[idx].state = BRICK_ALIVE;
            g->bricks[idx].row = (u8)r;
            g->bricks[idx].anim_time = 0;
            g->bricks[idx].anim_finished = false;
        }
    }
}

static void breakout_reset_ball(Breakout* g)
{
    g->ball_launched = false;
    g->ball_x = g->paddle_x + PADDLE_W / 2.0f - BALL_SIZE / 2.0f;
    g->ball_y = PADDLE_Y - BALL_SIZE;
    g->ball_dx = 0.0f;
    g->ball_dy = 0.0f;
}

static void breakout_init(Breakout* g, const Mel_Alloc* alloc)
{
    bool has_clip = g->death_clip.groups != NULL;
    Mel_Anim_Clip saved_clip = g->death_clip;
    u64 saved_prop = g->death_prop;

    memset(g, 0, sizeof(*g));
    g->alloc = alloc;
    g->rng_state = (u32)SDL_GetTicks();
    if (g->rng_state == 0) g->rng_state = 42;

    if (has_clip)
    {
        g->death_clip = saved_clip;
        g->death_prop = saved_prop;
    }
    else
    {
        mel_anim_registry_init(alloc);

        Mel_Sprite_Anim_Def def;
        u64 death_hash = mel_xxh3_64("death", 5);
        mel_sprite_anim_def_init(&def, death_hash, death_hash, alloc);
        mel_sprite_anim_def_push_frame(&def, 0, 0.06f);
        mel_sprite_anim_def_push_frame(&def, 1, 0.06f);
        mel_sprite_anim_def_push_frame(&def, 2, 0.06f);
        mel_sprite_anim_def_push_frame(&def, 3, 0.06f);
        mel_sprite_anim_def_push_frame(&def, 4, 0.06f);
        g->death_clip = mel_sprite_anim_def_compile(&def, alloc);
        g->death_prop = def.property_hash;
        mel_sprite_anim_def_destroy(&def);
    }

    g->lives = 3;
    g->score = 0;
    g->level = 1;
    g->ball_speed = BALL_INITIAL_SPEED;
    g->paddle_x = (f32)WIDTH / 2.0f - PADDLE_W / 2.0f;
    g->use_mouse = false;

    breakout_reset_bricks(g);
    breakout_reset_ball(g);
}

static void breakout_launch_ball(Breakout* g)
{
    if (g->ball_launched || g->game_over) return;

    f32 angle_offset = (f32)((i32)(breakout_rand(g) % 60) - 30);
    f32 angle = (-90.0f + angle_offset) * (3.14159265f / 180.0f);
    g->ball_dx = cosf(angle) * g->ball_speed;
    g->ball_dy = sinf(angle) * g->ball_speed;
    g->ball_launched = true;
}

static void breakout_kill_brick(Breakout* g, i32 idx)
{
    assert(g->bricks[idx].state == BRICK_ALIVE);
    g->bricks[idx].state = BRICK_DYING;
    g->bricks[idx].anim_time = 0;
    g->bricks[idx].anim_finished = false;
    g->bricks_alive--;
    g->score += 10 * (ROWS - g->bricks[idx].row);
}

static u32 breakout_death_frame(Breakout* g, f32 anim_time)
{
    Mel_Local_Pose pose;
    mel_pose_allocate(&pose, &g->death_clip, g->alloc);
    u32 cursor = 0;
    mel_anim_sample(&g->death_clip, anim_time, &cursor, g->alloc, &pose);
    f32 frame_f;
    mel_pose_extract_float(&pose, g->death_prop, &frame_f);
    return (u32)frame_f;
}

static f32 breakout_frame_alpha(u32 frame)
{
    switch (frame)
    {
        case 0: return 1.0f;
        case 1: return 0.75f;
        case 2: return 0.5f;
        case 3: return 0.25f;
        default: return 0.0f;
    }
}

static void breakout_next_level(Breakout* g)
{
    g->level++;
    g->ball_speed *= 1.15f;
    breakout_reset_bricks(g);
    breakout_reset_ball(g);
}

static void breakout_lose_life(Breakout* g)
{
    g->lives--;
    if (g->lives <= 0)
    {
        g->game_over = true;
        return;
    }
    breakout_reset_ball(g);
}

static void breakout_tick(Breakout* g, f32 dt)
{
    if (g->game_over) return;

    for (i32 i = 0; i < ROWS * COLS; i++)
    {
        if (g->bricks[i].state == BRICK_DYING)
        {
            g->bricks[i].anim_time += dt;
            if (g->bricks[i].anim_time >= g->death_clip.duration)
            {
                g->bricks[i].anim_finished = true;
                g->bricks[i].state = BRICK_DEAD;
            }
        }
    }

    if (g->bricks_alive == 0)
    {
        breakout_next_level(g);
        return;
    }

    if (!g->ball_launched)
    {
        g->ball_x = g->paddle_x + PADDLE_W / 2.0f - BALL_SIZE / 2.0f;
        g->ball_y = PADDLE_Y - BALL_SIZE;
        return;
    }

    f32 nx = g->ball_x + g->ball_dx * dt;
    f32 ny = g->ball_y + g->ball_dy * dt;

    if (nx < 0.0f)
    {
        nx = 0.0f;
        g->ball_dx = -g->ball_dx;
    }
    else if (nx + BALL_SIZE > (f32)WIDTH)
    {
        nx = (f32)WIDTH - BALL_SIZE;
        g->ball_dx = -g->ball_dx;
    }

    if (ny < 0.0f)
    {
        ny = 0.0f;
        g->ball_dy = -g->ball_dy;
    }

    if (ny + BALL_SIZE > (f32)HEIGHT)
    {
        breakout_lose_life(g);
        return;
    }

    if (g->ball_dy > 0.0f &&
        ny + BALL_SIZE >= PADDLE_Y &&
        ny + BALL_SIZE <= PADDLE_Y + PADDLE_H + g->ball_dy * dt &&
        nx + BALL_SIZE > g->paddle_x &&
        nx < g->paddle_x + PADDLE_W)
    {
        f32 hit_pos = (nx + BALL_SIZE / 2.0f - g->paddle_x) / PADDLE_W;
        f32 angle = (hit_pos - 0.5f) * 2.6f - 3.14159265f / 2.0f;
        g->ball_dx = cosf(angle) * g->ball_speed;
        g->ball_dy = sinf(angle) * g->ball_speed;
        if (g->ball_dy > -50.0f)
            g->ball_dy = -50.0f;
        ny = PADDLE_Y - BALL_SIZE;
    }

    for (i32 r = 0; r < ROWS; r++)
    {
        for (i32 c = 0; c < COLS; c++)
        {
            i32 idx = r * COLS + c;
            if (g->bricks[idx].state != BRICK_ALIVE) continue;

            f32 bx = brick_x(c);
            f32 by = brick_y(r);

            bool overlap_x = nx + BALL_SIZE > bx && nx < bx + BRICK_W;
            bool overlap_y = ny + BALL_SIZE > by && ny < by + BRICK_H;

            if (!overlap_x || !overlap_y) continue;

            breakout_kill_brick(g, idx);

            f32 prev_x = g->ball_x;
            f32 prev_y = g->ball_y;

            bool was_overlap_x = prev_x + BALL_SIZE > bx && prev_x < bx + BRICK_W;
            bool was_overlap_y = prev_y + BALL_SIZE > by && prev_y < by + BRICK_H;

            if (!was_overlap_x)
                g->ball_dx = -g->ball_dx;
            else if (!was_overlap_y)
                g->ball_dy = -g->ball_dy;
            else
                g->ball_dy = -g->ball_dy;

            goto done_brick_check;
        }
    }
    done_brick_check:

    g->ball_x = nx;
    g->ball_y = ny;
}

static void breakout_draw(Breakout* g, Mel_Render_List* list)
{
    Mel_Vec4 paddle_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mel_draw_sprite(list, .pos = mel_vec2(g->paddle_x, PADDLE_Y), .size = mel_vec2(PADDLE_W, PADDLE_H), .color = paddle_color);

    Mel_Vec4 ball_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mel_draw_sprite(list, .pos = mel_vec2(g->ball_x, g->ball_y), .size = mel_vec2(BALL_SIZE, BALL_SIZE), .color = ball_color);

    for (i32 r = 0; r < ROWS; r++)
    {
        for (i32 c = 0; c < COLS; c++)
        {
            i32 idx = r * COLS + c;
            if (g->bricks[idx].state == BRICK_DEAD) continue;

            f32 bx = brick_x(c);
            f32 by = brick_y(r);
            Mel_Vec4 color = ROW_COLORS[r];

            if (g->bricks[idx].state == BRICK_DYING)
            {
                u32 frame = breakout_death_frame(g, g->bricks[idx].anim_time);
                f32 alpha = breakout_frame_alpha(frame);
                if (alpha <= 0.0f) continue;

                if (frame == 0)
                    color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
                else
                    color.w = alpha;
            }

            mel_draw_sprite(list, .pos = mel_vec2(bx, by), .size = mel_vec2(BRICK_W, BRICK_H), .color = color);
        }
    }
}

static void breakout_draw_text(Breakout* g, Mel_Render_List* list, Mel_Font_Atlas_Pool* pool, Mel_Font_Handle font)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.6f, 0.6f, 0.6f, 1.0f);

    char buf[128];
    snprintf(buf, sizeof(buf), "SCORE: %u", g->score);
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), 10.0f, 10.0f, white);

    snprintf(buf, sizeof(buf), "LIVES: %d", g->lives);
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), (f32)WIDTH - 120.0f, 10.0f, white);

    snprintf(buf, sizeof(buf), "LEVEL: %u", g->level);
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), (f32)WIDTH / 2.0f - 40.0f, 10.0f, white);

    if (!g->ball_launched && !g->game_over)
    {
        str8 prompt = S8("SPACE to launch");
        Mel_Vec2 sz = mel_font_atlas_measure_text(pool, font, prompt);
        mel_font_atlas_draw_text(pool, font, list, prompt,
            (f32)WIDTH / 2.0f - sz.x / 2.0f, PADDLE_Y + 40.0f, dim);
    }

    if (g->game_over)
    {
        Mel_Vec4 red = mel_vec4(1.0f, 0.2f, 0.2f, 1.0f);
        str8 go_text = S8("GAME OVER");
        Mel_Vec2 go_sz = mel_font_atlas_measure_text(pool, font, go_text);
        mel_font_atlas_draw_text(pool, font, list, go_text,
            (f32)WIDTH / 2.0f - go_sz.x / 2.0f,
            (f32)HEIGHT / 2.0f - go_sz.y, red);

        snprintf(buf, sizeof(buf), "FINAL SCORE: %u", g->score);
        str8 score_text = str8_from_cstr(buf);
        Mel_Vec2 sc_sz = mel_font_atlas_measure_text(pool, font, score_text);
        mel_font_atlas_draw_text(pool, font, list, score_text,
            (f32)WIDTH / 2.0f - sc_sz.x / 2.0f,
            (f32)HEIGHT / 2.0f + 10.0f, white);

        str8 restart_text = S8("R to restart");
        Mel_Vec2 rs_sz = mel_font_atlas_measure_text(pool, font, restart_text);
        mel_font_atlas_draw_text(pool, font, list, restart_text,
            (f32)WIDTH / 2.0f - rs_sz.x / 2.0f,
            (f32)HEIGHT / 2.0f + 40.0f, dim);
    }
}

static void on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    s_font_handle = mel_font_atlas_pool_load(mel_font_pool(),
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 18.0f);


    breakout_init(&s_breakout, mel_alloc_heap());

    mel_render_list_init(&s_sprite_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_list_init(&s_font_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    s_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)sc->extent.width,
                                      (f32)sc->extent.height, 0, -1, 1),
    };

    s_sprite_source = mel_source_from_render_list(&s_sprite_list, MEL_SCHEMA_SPRITE);
    s_font_source = mel_source_from_render_list(&s_font_list, MEL_SCHEMA_SPRITE);

    s_main_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("main"),
        .camera = &s_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.08f, 0.08f, 0.1f, 1.0f),
    });
    mel_view_attach_source(s_main_view, s_sprite_source);
    mel_view_attach_source(s_main_view, s_font_source);

    s_frame_recipe = mel_frame_recipe_create(S8("breakout"));
    s_frame_plan = mel_frame_plan_create(S8("breakout"));
    mel_frame_recipe_use_technique(s_frame_recipe, s_main_view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_present(s_frame_recipe, s_main_view, s_swapchain_handle);

    mel_render_graph_init(&s_graph, .dev = dev, .alloc = mel_alloc_heap());
    mel_frame_plan_compile(s_frame_plan, s_frame_recipe, .graph = &s_graph, .dev = dev, .sprite_pass = mel_sprite_pass());
    mel_set_render_graph(&s_graph);

    SDL_Log("Breakout ready! Arrow keys / mouse to move, Space to launch, R to restart, ESC to quit");
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

void app_init(void)
{
    s_window_handle = mel_window_create(S8("Melody Breakout"), .width = WIDTH, .height = HEIGHT);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_window_handle);
    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, app_update);
    mel_register_sim(&s_sim);
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    mel_frame_recipe_destroy(s_frame_recipe);

    mel_render_list_shutdown(&s_sprite_list);
    mel_render_list_shutdown(&s_font_list);
    mel_render_graph_shutdown(&s_graph);
    mel_frame_plan_destroy(s_frame_plan);
    mel_view_destroy(s_main_view);
    mel_source_destroy(s_font_source);
    mel_source_destroy(s_sprite_source);

    mel_anim_clip_destroy(&s_breakout.death_clip, s_breakout.alloc);

    mel_vfs_unmount(S8("/"));
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    Breakout* g = &s_breakout;

    if (!g->game_over && !g->use_mouse)
    {
        const bool* keys = SDL_GetKeyboardState(NULL);
        f32 paddle_speed = 400.0f;
        if (keys[SDL_SCANCODE_LEFT])
            g->paddle_x -= paddle_speed * dt;
        if (keys[SDL_SCANCODE_RIGHT])
            g->paddle_x += paddle_speed * dt;
    }

    if (g->paddle_x < 0.0f) g->paddle_x = 0.0f;
    if (g->paddle_x + PADDLE_W > (f32)WIDTH) g->paddle_x = (f32)WIDTH - PADDLE_W;

    breakout_tick(g, dt);

    mel_render_list_clear(&s_sprite_list);
    mel_render_list_clear(&s_font_list);

    breakout_draw(g, &s_sprite_list);

    breakout_draw_text(g, &s_font_list, mel_font_pool(), s_font_handle);
}

void app_event(SDL_Event* event)
{
    Breakout* g = &s_breakout;

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        mel_quit();
        return;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        switch (event->key.scancode)
        {
            case SDL_SCANCODE_SPACE:
                breakout_launch_ball(g);
                break;
            case SDL_SCANCODE_R:
                breakout_init(g, g->alloc);
                break;
            case SDL_SCANCODE_LEFT:
            case SDL_SCANCODE_RIGHT:
                g->use_mouse = false;
                break;
            default: break;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        g->use_mouse = true;
        g->paddle_x = event->motion.x - PADDLE_W / 2.0f;
        if (g->paddle_x < 0.0f) g->paddle_x = 0.0f;
        if (g->paddle_x + PADDLE_W > (f32)WIDTH) g->paddle_x = (f32)WIDTH - PADDLE_W;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        g->use_mouse = true;
        breakout_launch_ball(g);
    }
}
