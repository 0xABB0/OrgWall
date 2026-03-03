#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <volk.h>

#include "core.app.h"
#include "core.engine.h"
#include "string.str8.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.buffer.h"
#include "gpu.texture.h"
#include "gpu.cmd.h"
#include "sprite.batch.h"
#include "font.atlas.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.vec2.h"
#include "anim.sprite.h"
#include "anim.clip.h"
#include "anim.track.h"
#include "allocator.heap.h"

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
    const Mel_Alloc* alloc;
    u32 rng_state;
    bool use_mouse;
} Breakout;

static SDL_Window* s_window;
static Mel_Gpu_Shader s_shader;
static Mel_Gpu_Pipeline s_pipeline;
static Mel_Gpu_Texture s_white_texture;
static Mel_SpriteBatch s_batch;
static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Io s_demo_io;
static Mel_Vfs s_demo_vfs;
static Mel_Vfs_Backend* s_fonts_backend;
static Mel_Font_Handle s_font_handle;
static Breakout s_breakout;

static const char* SHADER_SOURCE =
"struct VSInput\n"
"{\n"
"    float2 position : POSITION;\n"
"    float2 texcoord : TEXCOORD0;\n"
"    float4 color : COLOR0;\n"
"};\n"
"\n"
"struct VSOutput\n"
"{\n"
"    float4 position : SV_Position;\n"
"    float2 texcoord : TEXCOORD0;\n"
"    float4 color : COLOR0;\n"
"};\n"
"\n"
"struct PushConstants\n"
"{\n"
"    float4x4 projection;\n"
"};\n"
"\n"
"[[vk::push_constant]]\n"
"PushConstants push;\n"
"\n"
"[[vk::binding(0, 0)]] Sampler2D tex;\n"
"\n"
"[shader(\"vertex\")]\n"
"VSOutput vertexMain(VSInput input)\n"
"{\n"
"    VSOutput output;\n"
"    output.position = mul(push.projection, float4(input.position, 0.0, 1.0));\n"
"    output.texcoord = input.texcoord;\n"
"    output.color = input.color;\n"
"    return output;\n"
"}\n"
"\n"
"[shader(\"fragment\")]\n"
"float4 fragmentMain(VSOutput input) : SV_Target\n"
"{\n"
"    return tex.Sample(input.texcoord) * input.color;\n"
"}\n";

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
    if (g->death_clip.tracks)
        mel_anim_clip_destroy(&g->death_clip, alloc);

    memset(g, 0, sizeof(*g));
    g->alloc = alloc;
    g->rng_state = (u32)SDL_GetTicks();
    if (g->rng_state == 0) g->rng_state = 42;

    u32 death_frames[] = {0, 1, 2, 3, 4};
    f32 death_durs[]   = {0.06f, 0.06f, 0.06f, 0.06f, 0.06f};
    g->death_clip = mel_anim_sprite_clip(alloc, 0, death_frames, death_durs,
        DEATH_FRAME_COUNT, false);

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
    Mel_Anim_Track* track = mel_anim_clip_track(&g->death_clip, 0);
    f32 frame_f;
    mel_anim_track_eval(track, anim_time, &frame_f);
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

static void breakout_draw(Breakout* g, Mel_SpriteBatch* batch)
{
    Mel_Vec4 paddle_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mel_sprite_batch_draw(batch, g->paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H, paddle_color);

    Mel_Vec4 ball_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mel_sprite_batch_draw(batch, g->ball_x, g->ball_y, BALL_SIZE, BALL_SIZE, ball_color);

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

            mel_sprite_batch_draw(batch, bx, by, BRICK_W, BRICK_H, color);
        }
    }
}

static void breakout_draw_text(Breakout* g, Mel_SpriteBatch* batch, Mel_Font_Atlas_Entry* fe)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.6f, 0.6f, 0.6f, 1.0f);

    char buf[128];
    snprintf(buf, sizeof(buf), "SCORE: %u", g->score);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), 10.0f, 10.0f, white);

    snprintf(buf, sizeof(buf), "LIVES: %d", g->lives);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), (f32)WIDTH - 120.0f, 10.0f, white);

    snprintf(buf, sizeof(buf), "LEVEL: %u", g->level);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), (f32)WIDTH / 2.0f - 40.0f, 10.0f, white);

    if (!g->ball_launched && !g->game_over)
    {
        str8 prompt = S8("SPACE to launch");
        Mel_Vec2 sz = mel_font_atlas_measure_text(fe, prompt);
        mel_font_atlas_draw_text(fe, batch, prompt,
            (f32)WIDTH / 2.0f - sz.x / 2.0f, PADDLE_Y + 40.0f, dim);
    }

    if (g->game_over)
    {
        Mel_Vec4 red = mel_vec4(1.0f, 0.2f, 0.2f, 1.0f);
        str8 go_text = S8("GAME OVER");
        Mel_Vec2 go_sz = mel_font_atlas_measure_text(fe, go_text);
        mel_font_atlas_draw_text(fe, batch, go_text,
            (f32)WIDTH / 2.0f - go_sz.x / 2.0f,
            (f32)HEIGHT / 2.0f - go_sz.y, red);

        snprintf(buf, sizeof(buf), "FINAL SCORE: %u", g->score);
        str8 score_text = str8_from_cstr(buf);
        Mel_Vec2 sc_sz = mel_font_atlas_measure_text(fe, score_text);
        mel_font_atlas_draw_text(fe, batch, score_text,
            (f32)WIDTH / 2.0f - sc_sz.x / 2.0f,
            (f32)HEIGHT / 2.0f + 10.0f, white);

        str8 restart_text = S8("R to restart");
        Mel_Vec2 rs_sz = mel_font_atlas_measure_text(fe, restart_text);
        mel_font_atlas_draw_text(fe, batch, restart_text,
            (f32)WIDTH / 2.0f - rs_sz.x / 2.0f,
            (f32)HEIGHT / 2.0f + 40.0f, dim);
    }
}

static void on_init(Mel_Engine* e)
{
    Mel_Gpu_Device* dev = &e->dev;

    mel_gpu_shader_init(&s_shader, dev, .source = str8_from_cstr(SHADER_SOURCE));
    mel_gpu_texture_init_white(&s_white_texture, dev);

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Mel_SpriteVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attributes[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Mel_SpriteVertex, x) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Mel_SpriteVertex, u) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Mel_SpriteVertex, r) },
    };

    mel_gpu_pipeline_init(&s_pipeline, dev,
        .shader = &s_shader,
        .color_format = e->swapchain.format,
        .bindings = &binding,
        .binding_count = 1,
        .attributes = attributes,
        .attribute_count = 3,
        .push_constant_size = sizeof(Mel_Mat4),
        .use_texture = true,
        .blend_mode = MEL_GPU_BLEND_ALPHA);

    mel_sprite_batch_init(&s_batch, dev, .max_sprites = 2048);

    s_white_texture.descriptor = mel_gpu_pipeline_alloc_descriptor(&s_pipeline, dev);
    mel_gpu_pipeline_write_texture(&s_pipeline, dev, s_white_texture.descriptor,
        s_white_texture.image.view, s_white_texture.sampler);

    mel_font_atlas_pool_init(&s_font_pool, &e->allocator, dev, &s_demo_vfs);
    s_font_handle = mel_font_atlas_pool_load(&s_font_pool,
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 18.0f);

    Mel_Font_Atlas_Entry* font_entry = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (font_entry)
    {
        font_entry->atlas_texture.descriptor = mel_gpu_pipeline_alloc_descriptor(&s_pipeline, dev);
        mel_gpu_pipeline_write_texture(&s_pipeline, dev, font_entry->atlas_texture.descriptor,
            font_entry->atlas_texture.image.view, font_entry->atlas_texture.sampler);
    }

    breakout_init(&s_breakout, mel_alloc_heap());

    SDL_Log("Breakout ready! Arrow keys / mouse to move, Space to launch, R to restart, ESC to quit");
}

static void app_init(Mel_App* app)
{
    s_window = SDL_CreateWindow("Melody Breakout", WIDTH, HEIGHT,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    assert(s_window);

    mel_engine_init(&app->engine,
        .window = s_window,
        .app_name = S8("Melody Breakout"),
        .enable_validation = true,
        .enable_imgui = false,
        .fixed_dt = 1.0f / 60.0f);

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

    mel_anim_clip_destroy(&s_breakout.death_clip, s_breakout.alloc);

    mel_sprite_batch_shutdown(&s_batch, dev);
    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_vfs_unmount(&s_demo_vfs, S8("/"));
    mel_vfs_shutdown(&s_demo_vfs);
    mel_io_shutdown(&s_demo_io);
    mel_vfs_backend_os_destroy(s_fonts_backend);
    mel_gpu_texture_shutdown(&s_white_texture, dev);
    mel_gpu_pipeline_shutdown(&s_pipeline, dev);
    mel_gpu_shader_shutdown(&s_shader, dev);
}

static void app_update(Mel_App* app, f32 dt)
{
    MEL_UNUSED(app);

    Breakout* g = &s_breakout;

    if (!g->game_over && !g->use_mouse)
    {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        f32 paddle_speed = 400.0f;
        if (keys[SDL_SCANCODE_LEFT])
            g->paddle_x -= paddle_speed * dt;
        if (keys[SDL_SCANCODE_RIGHT])
            g->paddle_x += paddle_speed * dt;
    }

    if (g->paddle_x < 0.0f) g->paddle_x = 0.0f;
    if (g->paddle_x + PADDLE_W > (f32)WIDTH) g->paddle_x = (f32)WIDTH - PADDLE_W;

    breakout_tick(g, dt);
}

static void app_render(Mel_App* app, Mel_Gpu_Cmd* c)
{
    Mel_Engine* e = &app->engine;

    if (!s_pipeline.pipeline) return;

    mel_engine_begin_swapchain_pass(e, c,
        .clear_r = 0.08f, .clear_g = 0.08f, .clear_b = 0.1f, .clear_a = 1.0f);

    Mel_Mat4 proj = mel_mat4_ortho(0, (f32)e->swapchain.extent.width,
                                    0, (f32)e->swapchain.extent.height, -1, 1);

    mel_sprite_batch_begin(&s_batch, &s_pipeline);
    mel_sprite_batch_set_texture(&s_batch, &s_white_texture);
    breakout_draw(&s_breakout, &s_batch);

    Mel_Font_Atlas_Entry* fe = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (fe)
    {
        breakout_draw_text(&s_breakout, &s_batch, fe);
        mel_sprite_batch_set_texture(&s_batch, &s_white_texture);
    }

    mel_sprite_batch_end(&s_batch, &e->dev, c->cmd, &proj);
    mel_engine_end_swapchain_pass(e, c);
}

static void app_event(Mel_App* app, SDL_Event* event)
{
    Breakout* g = &s_breakout;

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        app->should_quit = true;
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

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_update = app_update,
    .on_render = app_render,
    .on_event = app_event
)
