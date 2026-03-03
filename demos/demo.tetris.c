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
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec4.h"

#define GRID_W 10
#define GRID_H 20
#define CELL_SIZE 28.0f
#define GRID_X_OFFSET 100.0f
#define GRID_Y_OFFSET 40.0f
#define PREVIEW_X (GRID_X_OFFSET + (GRID_W + 2) * CELL_SIZE)
#define PREVIEW_Y (GRID_Y_OFFSET + CELL_SIZE)

#define PIECE_I 1
#define PIECE_O 2
#define PIECE_T 3
#define PIECE_S 4
#define PIECE_Z 5
#define PIECE_L 6
#define PIECE_J 7

typedef struct {
    i8 cells[4][4];
    i32 size;
} Tetromino;

static const Tetromino TETROMINOES[7] = {
    [0] = { .cells = {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}}, .size = 4 },
    [1] = { .cells = {{1,1,0,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}}, .size = 2 },
    [2] = { .cells = {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}, .size = 3 },
    [3] = { .cells = {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}}, .size = 3 },
    [4] = { .cells = {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}, .size = 3 },
    [5] = { .cells = {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}, .size = 3 },
    [6] = { .cells = {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}, .size = 3 },
};

static Mel_Vec4 piece_color(u8 type)
{
    switch (type)
    {
        case PIECE_I: return mel_vec4(0.0f, 0.9f, 0.9f, 1.0f);
        case PIECE_O: return mel_vec4(0.9f, 0.9f, 0.0f, 1.0f);
        case PIECE_T: return mel_vec4(0.7f, 0.0f, 0.9f, 1.0f);
        case PIECE_S: return mel_vec4(0.0f, 0.9f, 0.0f, 1.0f);
        case PIECE_Z: return mel_vec4(0.9f, 0.0f, 0.0f, 1.0f);
        case PIECE_L: return mel_vec4(0.9f, 0.5f, 0.0f, 1.0f);
        case PIECE_J: return mel_vec4(0.0f, 0.0f, 0.9f, 1.0f);
        default:      return mel_vec4(0.3f, 0.3f, 0.3f, 1.0f);
    }
}

typedef struct {
    u8 grid[GRID_H][GRID_W];
    u8 piece_type;
    u8 next_type;
    Tetromino piece;
    i32 piece_x;
    i32 piece_y;
    i32 rotation;
    f32 drop_timer;
    f32 drop_interval;
    u32 score;
    u32 lines;
    u32 level;
    bool game_over;
    u32 rng_state;
} Tetris;

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
static Tetris s_tetris;

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

static u32 tetris_rand(Tetris* t)
{
    t->rng_state ^= t->rng_state << 13;
    t->rng_state ^= t->rng_state >> 17;
    t->rng_state ^= t->rng_state << 5;
    return t->rng_state;
}

static u8 random_piece(Tetris* t)
{
    return (u8)((tetris_rand(t) % 7) + 1);
}

static Tetromino rotate_piece(const Tetromino* src)
{
    Tetromino dst = { .size = src->size };
    i32 n = src->size;
    for (i32 r = 0; r < n; r++)
        for (i32 c = 0; c < n; c++)
            dst.cells[c][n - 1 - r] = src->cells[r][c];
    return dst;
}

static bool piece_fits(Tetris* t, const Tetromino* p, i32 px, i32 py)
{
    for (i32 r = 0; r < p->size; r++)
    {
        for (i32 c = 0; c < p->size; c++)
        {
            if (!p->cells[r][c]) continue;
            i32 gx = px + c;
            i32 gy = py + r;
            if (gx < 0 || gx >= GRID_W || gy >= GRID_H) return false;
            if (gy >= 0 && t->grid[gy][gx]) return false;
        }
    }
    return true;
}

static void lock_piece(Tetris* t)
{
    for (i32 r = 0; r < t->piece.size; r++)
    {
        for (i32 c = 0; c < t->piece.size; c++)
        {
            if (!t->piece.cells[r][c]) continue;
            i32 gx = t->piece_x + c;
            i32 gy = t->piece_y + r;
            if (gy >= 0 && gy < GRID_H && gx >= 0 && gx < GRID_W)
                t->grid[gy][gx] = t->piece_type;
        }
    }
}

static void clear_lines(Tetris* t)
{
    u32 cleared = 0;
    for (i32 row = GRID_H - 1; row >= 0; row--)
    {
        bool full = true;
        for (i32 col = 0; col < GRID_W; col++)
        {
            if (!t->grid[row][col]) { full = false; break; }
        }
        if (full)
        {
            for (i32 r = row; r > 0; r--)
                memcpy(t->grid[r], t->grid[r - 1], GRID_W);
            memset(t->grid[0], 0, GRID_W);
            cleared++;
            row++;
        }
    }

    if (cleared > 0)
    {
        static const u32 SCORES[] = { 0, 100, 300, 500, 800 };
        t->score += SCORES[cleared] * (t->level + 1);
        t->lines += cleared;
        t->level = t->lines / 10;
        t->drop_interval = 0.8f - (f32)t->level * 0.05f;
        if (t->drop_interval < 0.05f) t->drop_interval = 0.05f;
    }
}

static void spawn_piece(Tetris* t)
{
    t->piece_type = t->next_type;
    t->next_type = random_piece(t);
    t->piece = TETROMINOES[t->piece_type - 1];
    t->rotation = 0;
    t->piece_x = GRID_W / 2 - t->piece.size / 2;
    t->piece_y = -1;
    t->drop_timer = 0.0f;

    if (!piece_fits(t, &t->piece, t->piece_x, t->piece_y))
        t->game_over = true;
}

static void tetris_init(Tetris* t)
{
    memset(t, 0, sizeof(*t));
    t->rng_state = (u32)SDL_GetTicks();
    if (t->rng_state == 0) t->rng_state = 42;
    t->drop_interval = 0.8f;
    t->next_type = random_piece(t);
    spawn_piece(t);
}

static void tetris_move(Tetris* t, i32 dx)
{
    if (t->game_over) return;
    if (piece_fits(t, &t->piece, t->piece_x + dx, t->piece_y))
        t->piece_x += dx;
}

static void tetris_rotate(Tetris* t)
{
    if (t->game_over) return;
    Tetromino rotated = rotate_piece(&t->piece);
    if (piece_fits(t, &rotated, t->piece_x, t->piece_y))
    {
        t->piece = rotated;
        t->rotation = (t->rotation + 1) % 4;
        return;
    }
    for (i32 kick = 1; kick <= 2; kick++)
    {
        if (piece_fits(t, &rotated, t->piece_x - kick, t->piece_y))
        {
            t->piece = rotated;
            t->piece_x -= kick;
            t->rotation = (t->rotation + 1) % 4;
            return;
        }
        if (piece_fits(t, &rotated, t->piece_x + kick, t->piece_y))
        {
            t->piece = rotated;
            t->piece_x += kick;
            t->rotation = (t->rotation + 1) % 4;
            return;
        }
    }
}

static void tetris_soft_drop(Tetris* t)
{
    if (t->game_over) return;
    if (piece_fits(t, &t->piece, t->piece_x, t->piece_y + 1))
    {
        t->piece_y++;
        t->drop_timer = 0.0f;
    }
}

static void tetris_hard_drop(Tetris* t)
{
    if (t->game_over) return;
    while (piece_fits(t, &t->piece, t->piece_x, t->piece_y + 1))
        t->piece_y++;
    lock_piece(t);
    clear_lines(t);
    spawn_piece(t);
}

static i32 ghost_y(Tetris* t)
{
    i32 gy = t->piece_y;
    while (piece_fits(t, &t->piece, t->piece_x, gy + 1))
        gy++;
    return gy;
}

static void tetris_tick(Tetris* t, f32 dt)
{
    if (t->game_over) return;

    t->drop_timer += dt;
    while (t->drop_timer >= t->drop_interval)
    {
        t->drop_timer -= t->drop_interval;
        if (piece_fits(t, &t->piece, t->piece_x, t->piece_y + 1))
        {
            t->piece_y++;
        }
        else
        {
            lock_piece(t);
            clear_lines(t);
            spawn_piece(t);
        }
    }
}

static void draw_cell(Mel_SpriteBatch* batch, i32 gx, i32 gy, Mel_Vec4 color)
{
    f32 x = GRID_X_OFFSET + (f32)gx * CELL_SIZE;
    f32 y = GRID_Y_OFFSET + (f32)gy * CELL_SIZE;
    f32 pad = 1.0f;
    mel_sprite_batch_draw(batch, x + pad, y + pad, CELL_SIZE - pad * 2, CELL_SIZE - pad * 2, color);
}

static void tetris_draw_grid(Tetris* t, Mel_SpriteBatch* batch)
{
    Mel_Vec4 bg = mel_vec4(0.05f, 0.05f, 0.08f, 1.0f);
    mel_sprite_batch_draw(batch,
        GRID_X_OFFSET, GRID_Y_OFFSET,
        (f32)GRID_W * CELL_SIZE, (f32)GRID_H * CELL_SIZE, bg);

    Mel_Vec4 line_color = mel_vec4(0.12f, 0.12f, 0.15f, 1.0f);
    for (i32 row = 0; row < GRID_H; row++)
    {
        for (i32 col = 0; col < GRID_W; col++)
        {
            f32 x = GRID_X_OFFSET + (f32)col * CELL_SIZE;
            f32 y = GRID_Y_OFFSET + (f32)row * CELL_SIZE;
            mel_sprite_batch_draw(batch, x, y, CELL_SIZE, 1.0f, line_color);
            mel_sprite_batch_draw(batch, x, y, 1.0f, CELL_SIZE, line_color);
        }
    }

    for (i32 row = 0; row < GRID_H; row++)
        for (i32 col = 0; col < GRID_W; col++)
            if (t->grid[row][col])
                draw_cell(batch, col, row, piece_color(t->grid[row][col]));

    if (!t->game_over)
    {
        i32 gy = ghost_y(t);
        Mel_Vec4 ghost_col = piece_color(t->piece_type);
        ghost_col.w = 0.25f;
        for (i32 r = 0; r < t->piece.size; r++)
            for (i32 c = 0; c < t->piece.size; c++)
                if (t->piece.cells[r][c] && (gy + r) >= 0)
                    draw_cell(batch, t->piece_x + c, gy + r, ghost_col);

        Mel_Vec4 pc = piece_color(t->piece_type);
        for (i32 r = 0; r < t->piece.size; r++)
            for (i32 c = 0; c < t->piece.size; c++)
                if (t->piece.cells[r][c] && (t->piece_y + r) >= 0)
                    draw_cell(batch, t->piece_x + c, t->piece_y + r, pc);
    }

    const Tetromino* nxt = &TETROMINOES[t->next_type - 1];
    Mel_Vec4 nc = piece_color(t->next_type);
    f32 preview_cell = 20.0f;
    for (i32 r = 0; r < nxt->size; r++)
        for (i32 c = 0; c < nxt->size; c++)
            if (nxt->cells[r][c])
                mel_sprite_batch_draw(batch,
                    PREVIEW_X + (f32)c * preview_cell,
                    PREVIEW_Y + 20.0f + (f32)r * preview_cell,
                    preview_cell - 2.0f, preview_cell - 2.0f, nc);
}

static void tetris_draw_text(Tetris* t, Mel_SpriteBatch* batch, Mel_Font_Atlas_Entry* fe)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.6f, 0.6f, 0.6f, 1.0f);

    mel_font_atlas_draw_text(fe, batch, S8("NEXT"), PREVIEW_X, GRID_Y_OFFSET, dim);

    char buf[64];
    snprintf(buf, sizeof(buf), "SCORE\n%u", t->score);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), PREVIEW_X, PREVIEW_Y + 120.0f, white);

    snprintf(buf, sizeof(buf), "LEVEL\n%u", t->level);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), PREVIEW_X, PREVIEW_Y + 200.0f, white);

    snprintf(buf, sizeof(buf), "LINES\n%u", t->lines);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), PREVIEW_X, PREVIEW_Y + 280.0f, white);

    if (t->game_over)
    {
        Mel_Vec4 red = mel_vec4(1.0f, 0.2f, 0.2f, 1.0f);
        f32 cx = GRID_X_OFFSET + 30.0f;
        f32 cy = GRID_Y_OFFSET + GRID_H * CELL_SIZE / 2.0f - 10.0f;
        mel_font_atlas_draw_text(fe, batch, S8("GAME OVER"), cx, cy, red);
        mel_font_atlas_draw_text(fe, batch, S8("R to restart"), cx + 10.0f, cy + 30.0f, dim);
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

    tetris_init(&s_tetris);

    SDL_Log("Tetris ready! Arrow keys to move/rotate, Space to hard drop, R to restart, ESC to quit");
}

static void app_init(Mel_App* app)
{
    s_window = SDL_CreateWindow("Melody Tetris", 520, 640,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    assert(s_window);

    mel_engine_init(&app->engine,
        .window = s_window,
        .app_name = S8("Melody Tetris"),
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
    tetris_tick(&s_tetris, dt);
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
    tetris_draw_grid(&s_tetris, &s_batch);

    Mel_Font_Atlas_Entry* fe = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (fe)
        tetris_draw_text(&s_tetris, &s_batch, fe);

    mel_sprite_batch_end(&s_batch, &e->dev, c->cmd, &proj);
    mel_engine_end_swapchain_pass(e, c);
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
        switch (event->key.scancode)
        {
            case SDL_SCANCODE_LEFT:  tetris_move(&s_tetris, -1); break;
            case SDL_SCANCODE_RIGHT: tetris_move(&s_tetris, +1); break;
            case SDL_SCANCODE_UP:    tetris_rotate(&s_tetris); break;
            case SDL_SCANCODE_DOWN:  tetris_soft_drop(&s_tetris); break;
            case SDL_SCANCODE_SPACE: tetris_hard_drop(&s_tetris); break;
            case SDL_SCANCODE_R:     tetris_init(&s_tetris); break;
            default: break;
        }
    }

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.repeat)
    {
        switch (event->key.scancode)
        {
            case SDL_SCANCODE_LEFT:  tetris_move(&s_tetris, -1); break;
            case SDL_SCANCODE_RIGHT: tetris_move(&s_tetris, +1); break;
            case SDL_SCANCODE_DOWN:  tetris_soft_drop(&s_tetris); break;
            default: break;
        }
    }
}

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_update = app_update,
    .on_render = app_render,
    .on_event = app_event
)
