#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

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
#include "render.list.h"
#include "render.target.h"
#include "render.camera.h"
#include "texture.pool.h"
#include "font.atlas.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "sim.ctx.h"

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
static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Io s_demo_io;
static Mel_Vfs s_demo_vfs;
static Mel_Vfs_Backend* s_fonts_backend;
static Mel_Font_Handle s_font_handle;
static Tetris s_tetris;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static Mel_Render_Target s_swapchain_target;
static Mel_Render_Graph s_graph;
static Mel_Camera s_camera;
static Mel_Render_List s_sprite_list;
static Mel_Render_List s_font_list;

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

static void draw_cell(Mel_Render_List* list, i32 gx, i32 gy, Mel_Vec4 color)
{
    f32 x = GRID_X_OFFSET + (f32)gx * CELL_SIZE;
    f32 y = GRID_Y_OFFSET + (f32)gy * CELL_SIZE;
    f32 pad = 1.0f;
    Mel_Sprite_Entry* e = mel_render_list_push(list, mel_sort_key_sprite(0, 0.0f, 0, 0));
    *e = (Mel_Sprite_Entry){
        .pos = mel_vec2(x + pad, y + pad),
        .size = mel_vec2(CELL_SIZE - pad * 2, CELL_SIZE - pad * 2),
        .uv = MEL_UV_FULL,
        .color = color,
    };
}

static void tetris_draw_grid(Tetris* t, Mel_Render_List* list)
{
    Mel_Vec4 bg = mel_vec4(0.05f, 0.05f, 0.08f, 1.0f);
    Mel_Sprite_Entry* e = mel_render_list_push(list, mel_sort_key_sprite(0, 0.0f, 0, 0));
    *e = (Mel_Sprite_Entry){
        .pos = mel_vec2(GRID_X_OFFSET, GRID_Y_OFFSET),
        .size = mel_vec2((f32)GRID_W * CELL_SIZE, (f32)GRID_H * CELL_SIZE),
        .uv = MEL_UV_FULL,
        .color = bg,
    };

    Mel_Vec4 line_color = mel_vec4(0.12f, 0.12f, 0.15f, 1.0f);
    for (i32 row = 0; row < GRID_H; row++)
    {
        for (i32 col = 0; col < GRID_W; col++)
        {
            f32 x = GRID_X_OFFSET + (f32)col * CELL_SIZE;
            f32 y = GRID_Y_OFFSET + (f32)row * CELL_SIZE;

            Mel_Sprite_Entry* h = mel_render_list_push(list, mel_sort_key_sprite(0, 0.0f, 0, 0));
            *h = (Mel_Sprite_Entry){
                .pos = mel_vec2(x, y), .size = mel_vec2(CELL_SIZE, 1.0f),
                .uv = MEL_UV_FULL, .color = line_color,
            };

            Mel_Sprite_Entry* v = mel_render_list_push(list, mel_sort_key_sprite(0, 0.0f, 0, 0));
            *v = (Mel_Sprite_Entry){
                .pos = mel_vec2(x, y), .size = mel_vec2(1.0f, CELL_SIZE),
                .uv = MEL_UV_FULL, .color = line_color,
            };
        }
    }

    for (i32 row = 0; row < GRID_H; row++)
        for (i32 col = 0; col < GRID_W; col++)
            if (t->grid[row][col])
                draw_cell(list, col, row, piece_color(t->grid[row][col]));

    if (!t->game_over)
    {
        i32 gy = ghost_y(t);
        Mel_Vec4 ghost_col = piece_color(t->piece_type);
        ghost_col.w = 0.25f;
        for (i32 r = 0; r < t->piece.size; r++)
            for (i32 c = 0; c < t->piece.size; c++)
                if (t->piece.cells[r][c] && (gy + r) >= 0)
                    draw_cell(list, t->piece_x + c, gy + r, ghost_col);

        Mel_Vec4 pc = piece_color(t->piece_type);
        for (i32 r = 0; r < t->piece.size; r++)
            for (i32 c = 0; c < t->piece.size; c++)
                if (t->piece.cells[r][c] && (t->piece_y + r) >= 0)
                    draw_cell(list, t->piece_x + c, t->piece_y + r, pc);
    }

    const Tetromino* nxt = &TETROMINOES[t->next_type - 1];
    Mel_Vec4 nc = piece_color(t->next_type);
    f32 preview_cell = 20.0f;
    for (i32 r = 0; r < nxt->size; r++)
    {
        for (i32 c = 0; c < nxt->size; c++)
        {
            if (nxt->cells[r][c])
            {
                Mel_Sprite_Entry* pe = mel_render_list_push(list, mel_sort_key_sprite(0, 0.0f, 0, 0));
                *pe = (Mel_Sprite_Entry){
                    .pos = mel_vec2(PREVIEW_X + (f32)c * preview_cell, PREVIEW_Y + 20.0f + (f32)r * preview_cell),
                    .size = mel_vec2(preview_cell - 2.0f, preview_cell - 2.0f),
                    .uv = MEL_UV_FULL,
                    .color = nc,
                };
            }
        }
    }
}

static void tetris_draw_text(Tetris* t, Mel_Render_List* list, Mel_Font_Atlas_Pool* pool, Mel_Font_Handle font)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.6f, 0.6f, 0.6f, 1.0f);

    mel_font_atlas_draw_text(pool, font, list, S8("NEXT"), PREVIEW_X, GRID_Y_OFFSET, dim);

    char buf[64];
    snprintf(buf, sizeof(buf), "SCORE\n%u", t->score);
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), PREVIEW_X, PREVIEW_Y + 120.0f, white);

    snprintf(buf, sizeof(buf), "LEVEL\n%u", t->level);
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), PREVIEW_X, PREVIEW_Y + 200.0f, white);

    snprintf(buf, sizeof(buf), "LINES\n%u", t->lines);
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(buf), PREVIEW_X, PREVIEW_Y + 280.0f, white);

    if (t->game_over)
    {
        Mel_Vec4 red = mel_vec4(1.0f, 0.2f, 0.2f, 1.0f);
        f32 cx = GRID_X_OFFSET + 30.0f;
        f32 cy = GRID_Y_OFFSET + GRID_H * CELL_SIZE / 2.0f - 10.0f;
        mel_font_atlas_draw_text(pool, font, list, S8("GAME OVER"), cx, cy, red);
        mel_font_atlas_draw_text(pool, font, list, S8("R to restart"), cx + 10.0f, cy + 30.0f, dim);
    }
}

static void on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    mel_font_atlas_pool_init(&s_font_pool, mel_allocator(), dev, &s_demo_vfs, .texture_pool = mel_texture_pool());
    s_font_handle = mel_font_atlas_pool_load(&s_font_pool,
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 18.0f);


    tetris_init(&s_tetris);

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
    mel_render_graph_add_pass(&s_graph, S8("render"),
        .fn = mel_sprite_pass_execute,
        .camera = &s_camera,
        .read_lists = MEL_LISTS(&s_sprite_list, &s_font_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.08f, .g = 0.08f, .b = 0.1f, .a = 1.0f } }));
    mel_render_graph_compile(&s_graph);
    mel_set_render_graph(&s_graph);

    SDL_Log("Tetris ready! Arrow keys to move/rotate, Space to hard drop, R to restart, ESC to quit");
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

static void app_init(Mel_App* app)
{
    mel_init(.app_name = S8("Melody Tetris"), .enable_validation = true);
    s_window_handle = mel_window_create(S8("Melody Tetris"), .width = 520, .height = 640);
    s_window = mel_window_get(s_window_handle)->sdl;
    s_swapchain_handle = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_window_handle);

    Mel_Io_Desc io_desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&s_demo_io, &io_desc);
    Mel_Vfs_Desc vfs_desc = { .allocator = mel_alloc_heap(), .io = &s_demo_io };
    mel_vfs_init(&s_demo_vfs, &vfs_desc);
    s_fonts_backend = mel_vfs_backend_os_create(mel_alloc_heap(), S8("/"));
    mel_vfs_mount(&s_demo_vfs, S8("/"), s_fonts_backend, 0, false);

    on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, app_update);
    mel_register_sim(&s_sim);
}

static void app_shutdown(Mel_App* app)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    mel_render_list_shutdown(&s_sprite_list);
    mel_render_list_shutdown(&s_font_list);
    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_swapchain_target);

    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_vfs_unmount(&s_demo_vfs, S8("/"));
    mel_vfs_shutdown(&s_demo_vfs);
    mel_io_shutdown(&s_demo_io);
    mel_vfs_backend_os_destroy(s_fonts_backend);
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);
    tetris_tick(&s_tetris, dt);

    mel_render_list_clear(&s_sprite_list);
    mel_render_list_clear(&s_font_list);

    tetris_draw_grid(&s_tetris, &s_sprite_list);

    tetris_draw_text(&s_tetris, &s_font_list, &s_font_pool, s_font_handle);
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
    .on_event = app_event
)
