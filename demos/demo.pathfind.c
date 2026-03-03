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
#include "async.coro.h"
#include "allocator.heap.h"

#include <string.h>
#include <math.h>

#define WIDTH  800
#define HEIGHT 600

#define GRID_W 30
#define GRID_H 22
#define CELL_SIZE 24.0f
#define GRID_X_OFFSET 20.0f
#define GRID_Y_OFFSET 20.0f

#define CELL_EMPTY 0
#define CELL_WALL  1

#define SEARCH_NONE    0
#define SEARCH_RUNNING 1
#define SEARCH_DONE    2
#define SEARCH_NOPATH  3

#define TILE_COLS 16
#define TILE_ROWS 16

typedef struct { i32 x, y; } GridPos;

typedef struct {
    i32 parent_x, parent_y;
    f32 g, f;
    bool in_open, in_closed;
} AStarNode;

typedef struct {
    u8 cells[GRID_H][GRID_W];
    AStarNode nodes[GRID_H][GRID_W];
    GridPos open_list[GRID_W * GRID_H];
    i32 open_count;
    GridPos start, end;
    bool has_start, has_end;
    u32 search_state;
    GridPos path[GRID_W * GRID_H];
    i32 path_length;
    i32 nodes_explored;
    Mel_Coro_Context* coro;
    bool right_click_is_start;
} Pathfinder;

static SDL_Window* s_window;
static Mel_Gpu_Shader s_shader;
static Mel_Gpu_Pipeline s_pipeline;
static Mel_Gpu_Texture s_white_texture;
static Mel_Gpu_Texture s_grass_texture;
static Mel_Gpu_Texture s_stone_texture;
static Mel_SpriteBatch s_batch;
static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Io s_demo_io;
static Mel_Vfs s_demo_vfs;
static Mel_Vfs_Backend* s_fonts_backend;
static Mel_Font_Handle s_font_handle;
static Pathfinder s_pf;

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

static f32 heuristic(GridPos a, GridPos b)
{
    return (f32)(abs(a.x - b.x) + abs(a.y - b.y));
}

static i32 open_list_find_min(Pathfinder* pf)
{
    assert(pf->open_count > 0);
    i32 best = 0;
    f32 best_f = pf->nodes[pf->open_list[0].y][pf->open_list[0].x].f;
    for (i32 i = 1; i < pf->open_count; i++)
    {
        f32 cf = pf->nodes[pf->open_list[i].y][pf->open_list[i].x].f;
        if (cf < best_f)
        {
            best_f = cf;
            best = i;
        }
    }
    return best;
}

static void open_list_remove(Pathfinder* pf, i32 idx)
{
    pf->open_list[idx] = pf->open_list[pf->open_count - 1];
    pf->open_count--;
}

static void trace_path(Pathfinder* pf)
{
    pf->path_length = 0;
    GridPos cur = pf->end;
    while (cur.x != pf->start.x || cur.y != pf->start.y)
    {
        assert(pf->path_length < GRID_W * GRID_H);
        pf->path[pf->path_length++] = cur;
        AStarNode* n = &pf->nodes[cur.y][cur.x];
        cur.x = n->parent_x;
        cur.y = n->parent_y;
    }
    pf->path[pf->path_length++] = pf->start;
}

static const i32 DX[4] = { 0, 0, -1, 1 };
static const i32 DY[4] = { -1, 1, 0, 0 };

mel_coro_declare(astar_solve)
{
    Pathfinder* pf = mel_coro_userdata();

    memset(pf->nodes, 0, sizeof(pf->nodes));
    pf->open_count = 0;
    pf->path_length = 0;
    pf->nodes_explored = 0;
    pf->search_state = SEARCH_RUNNING;

    AStarNode* start_node = &pf->nodes[pf->start.y][pf->start.x];
    start_node->g = 0.0f;
    start_node->f = heuristic(pf->start, pf->end);
    start_node->in_open = true;
    start_node->parent_x = pf->start.x;
    start_node->parent_y = pf->start.y;

    pf->open_list[0] = pf->start;
    pf->open_count = 1;

    while (pf->open_count > 0)
    {
        i32 best_idx = open_list_find_min(pf);
        GridPos current = pf->open_list[best_idx];
        open_list_remove(pf, best_idx);

        AStarNode* cur_node = &pf->nodes[current.y][current.x];
        cur_node->in_open = false;
        cur_node->in_closed = true;
        pf->nodes_explored++;

        if (current.x == pf->end.x && current.y == pf->end.y)
        {
            trace_path(pf);
            pf->search_state = SEARCH_DONE;
            mel_coro_end(pf->coro);
        }

        for (i32 d = 0; d < 4; d++)
        {
            i32 nx = current.x + DX[d];
            i32 ny = current.y + DY[d];

            if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) continue;
            if (pf->cells[ny][nx] == CELL_WALL) continue;

            AStarNode* neighbor = &pf->nodes[ny][nx];
            if (neighbor->in_closed) continue;

            f32 tentative_g = cur_node->g + 1.0f;

            if (!neighbor->in_open)
            {
                neighbor->g = tentative_g;
                neighbor->f = tentative_g + heuristic((GridPos){nx, ny}, pf->end);
                neighbor->parent_x = current.x;
                neighbor->parent_y = current.y;
                neighbor->in_open = true;
                pf->open_list[pf->open_count++] = (GridPos){nx, ny};
            }
            else if (tentative_g < neighbor->g)
            {
                neighbor->g = tentative_g;
                neighbor->f = tentative_g + heuristic((GridPos){nx, ny}, pf->end);
                neighbor->parent_x = current.x;
                neighbor->parent_y = current.y;
            }
        }

        mel_coro_yield(pf->coro);
    }

    pf->search_state = SEARCH_NOPATH;
    mel_coro_end(pf->coro);
}

static void pathfinder_reset_search(Pathfinder* pf)
{
    memset(pf->nodes, 0, sizeof(pf->nodes));
    pf->open_count = 0;
    pf->path_length = 0;
    pf->nodes_explored = 0;
    pf->search_state = SEARCH_NONE;

    if (pf->coro)
    {
        mel_coro_destroy(pf->coro);
        pf->coro = mel_coro_create(mel_alloc_heap(), .num_initial = 1);
    }
}

static void pathfinder_clear(Pathfinder* pf)
{
    memset(pf->cells, 0, sizeof(pf->cells));
    pf->has_start = false;
    pf->has_end = false;
    pf->right_click_is_start = true;
    pathfinder_reset_search(pf);
}

static void pathfinder_init(Pathfinder* pf)
{
    memset(pf, 0, sizeof(*pf));
    pf->right_click_is_start = true;
    pf->coro = mel_coro_create(mel_alloc_heap(), .num_initial = 1);
}

static void draw_tile(Mel_SpriteBatch* batch, f32 x, f32 y, f32 w, f32 h, i32 tile_col, i32 tile_row)
{
    f32 u0 = (f32)tile_col / (f32)TILE_COLS;
    f32 v0 = (f32)tile_row / (f32)TILE_ROWS;
    f32 u1 = (f32)(tile_col + 1) / (f32)TILE_COLS;
    f32 v1 = (f32)(tile_row + 1) / (f32)TILE_ROWS;
    mel_sprite_batch_draw_uv(batch, x, y, w, h, u0, v0, u1, v1, mel_vec4(1.0f, 1.0f, 1.0f, 1.0f));
}

static void pathfinder_draw_tiles(Pathfinder* pf, Mel_SpriteBatch* batch, Mel_Gpu_Device* dev)
{
    mel_sprite_batch_set_texture(batch, &s_grass_texture);
    for (i32 row = 0; row < GRID_H; row++)
    {
        for (i32 col = 0; col < GRID_W; col++)
        {
            if (pf->cells[row][col] != CELL_EMPTY) continue;
            f32 x = GRID_X_OFFSET + (f32)col * CELL_SIZE;
            f32 y = GRID_Y_OFFSET + (f32)row * CELL_SIZE;
            draw_tile(batch, x, y, CELL_SIZE, CELL_SIZE, 0, 0);
        }
    }

    mel_sprite_batch_set_texture(batch, &s_stone_texture);
    for (i32 row = 0; row < GRID_H; row++)
    {
        for (i32 col = 0; col < GRID_W; col++)
        {
            if (pf->cells[row][col] != CELL_WALL) continue;
            f32 x = GRID_X_OFFSET + (f32)col * CELL_SIZE;
            f32 y = GRID_Y_OFFSET + (f32)row * CELL_SIZE;
            draw_tile(batch, x, y, CELL_SIZE, CELL_SIZE, 0, 0);
        }
    }
}

static void pathfinder_draw_overlays(Pathfinder* pf, Mel_SpriteBatch* batch, Mel_Gpu_Device* dev)
{
    mel_sprite_batch_set_texture(batch, &s_white_texture);

    if (pf->search_state != SEARCH_NONE)
    {
        for (i32 row = 0; row < GRID_H; row++)
        {
            for (i32 col = 0; col < GRID_W; col++)
            {
                AStarNode* n = &pf->nodes[row][col];
                f32 x = GRID_X_OFFSET + (f32)col * CELL_SIZE;
                f32 y = GRID_Y_OFFSET + (f32)row * CELL_SIZE;

                if (n->in_closed)
                    mel_sprite_batch_draw(batch, x, y, CELL_SIZE, CELL_SIZE,
                        mel_vec4(0.9f, 0.3f, 0.5f, 0.3f));
                else if (n->in_open)
                    mel_sprite_batch_draw(batch, x, y, CELL_SIZE, CELL_SIZE,
                        mel_vec4(0.2f, 0.3f, 0.9f, 0.4f));
            }
        }

        for (i32 i = 0; i < pf->path_length; i++)
        {
            GridPos p = pf->path[i];
            f32 x = GRID_X_OFFSET + (f32)p.x * CELL_SIZE;
            f32 y = GRID_Y_OFFSET + (f32)p.y * CELL_SIZE;
            mel_sprite_batch_draw(batch, x, y, CELL_SIZE, CELL_SIZE,
                mel_vec4(1.0f, 0.9f, 0.1f, 0.6f));
        }
    }

    if (pf->has_start)
    {
        f32 x = GRID_X_OFFSET + (f32)pf->start.x * CELL_SIZE;
        f32 y = GRID_Y_OFFSET + (f32)pf->start.y * CELL_SIZE;
        mel_sprite_batch_draw(batch, x, y, CELL_SIZE, CELL_SIZE,
            mel_vec4(0.0f, 0.8f, 0.0f, 0.5f));
    }

    if (pf->has_end)
    {
        f32 x = GRID_X_OFFSET + (f32)pf->end.x * CELL_SIZE;
        f32 y = GRID_Y_OFFSET + (f32)pf->end.y * CELL_SIZE;
        mel_sprite_batch_draw(batch, x, y, CELL_SIZE, CELL_SIZE,
            mel_vec4(0.8f, 0.0f, 0.0f, 0.5f));
    }
}

static void pathfinder_draw_grid_lines(Mel_SpriteBatch* batch, Mel_Gpu_Device* dev)
{
    mel_sprite_batch_set_texture(batch, &s_white_texture);
    Mel_Vec4 line_color = mel_vec4(0.0f, 0.0f, 0.0f, 0.15f);

    for (i32 row = 0; row <= GRID_H; row++)
    {
        f32 y = GRID_Y_OFFSET + (f32)row * CELL_SIZE;
        mel_sprite_batch_draw(batch,
            GRID_X_OFFSET, y,
            (f32)GRID_W * CELL_SIZE, 1.0f, line_color);
    }

    for (i32 col = 0; col <= GRID_W; col++)
    {
        f32 x = GRID_X_OFFSET + (f32)col * CELL_SIZE;
        mel_sprite_batch_draw(batch,
            x, GRID_Y_OFFSET,
            1.0f, (f32)GRID_H * CELL_SIZE, line_color);
    }
}

static void pathfinder_draw_text(Pathfinder* pf, Mel_SpriteBatch* batch,
                                  Mel_Gpu_Device* dev, Mel_Font_Atlas_Entry* fe)
{
    mel_sprite_batch_set_texture(batch, &fe->atlas_texture);

    f32 text_y = GRID_Y_OFFSET + (f32)GRID_H * CELL_SIZE + 8.0f;
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.6f, 0.6f, 0.6f, 1.0f);

    char buf[256];
    snprintf(buf, sizeof(buf), "Nodes: %d  Path: %d", pf->nodes_explored, pf->path_length);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), GRID_X_OFFSET, text_y, white);

    const char* state_str = "";
    Mel_Vec4 state_color = dim;
    if (pf->search_state == SEARCH_RUNNING)
    {
        state_str = "Searching...";
        state_color = mel_vec4(0.2f, 0.6f, 1.0f, 1.0f);
    }
    else if (pf->search_state == SEARCH_DONE)
    {
        state_str = "Path found!";
        state_color = mel_vec4(0.2f, 1.0f, 0.2f, 1.0f);
    }
    else if (pf->search_state == SEARCH_NOPATH)
    {
        state_str = "No path!";
        state_color = mel_vec4(1.0f, 0.3f, 0.3f, 1.0f);
    }

    if (state_str[0] != '\0')
    {
        mel_font_atlas_draw_text(fe, batch, str8_from_cstr(state_str),
            GRID_X_OFFSET + 300.0f, text_y, state_color);
    }

    mel_font_atlas_draw_text(fe, batch,
        S8("LMB: wall  RMB: start/end  SPACE: solve  R: reset  C: clear  ESC: quit"),
        GRID_X_OFFSET, text_y + 22.0f, dim);
}

static void setup_texture_descriptor(Mel_Gpu_Texture* tex, Mel_Gpu_Device* dev)
{
    tex->descriptor = mel_gpu_pipeline_alloc_descriptor(&s_pipeline, dev);
    mel_gpu_pipeline_write_texture(&s_pipeline, dev, tex->descriptor,
        tex->image.view, tex->sampler);
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

    mel_sprite_batch_init(&s_batch, dev, .max_sprites = 4096);

    setup_texture_descriptor(&s_white_texture, dev);

    mel_gpu_texture_init(&s_grass_texture, dev,
        .path = S8("assets/tilesets/grass.png"), .nearest_filter = true);
    setup_texture_descriptor(&s_grass_texture, dev);

    mel_gpu_texture_init(&s_stone_texture, dev,
        .path = S8("assets/tilesets/wall.png"), .nearest_filter = true);
    setup_texture_descriptor(&s_stone_texture, dev);

    mel_font_atlas_pool_init(&s_font_pool, &e->allocator, dev, &s_demo_vfs);
    s_font_handle = mel_font_atlas_pool_load(&s_font_pool,
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 16.0f);

    Mel_Font_Atlas_Entry* font_entry = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (font_entry)
        setup_texture_descriptor(&font_entry->atlas_texture, dev);

    pathfinder_init(&s_pf);

    SDL_Log("A* Pathfinder ready! LMB: walls, RMB: start/end, Space: solve, R: reset, C: clear");
}

static void app_init(Mel_App* app)
{
    s_window = SDL_CreateWindow("Melody A* Pathfinder", WIDTH, HEIGHT,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    assert(s_window);

    mel_engine_init(&app->engine,
        .window = s_window,
        .app_name = S8("Melody A* Pathfinder"),
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

    mel_coro_destroy(s_pf.coro);
    s_pf.coro = NULL;

    mel_sprite_batch_shutdown(&s_batch, dev);
    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_vfs_unmount(&s_demo_vfs, S8("/"));
    mel_vfs_shutdown(&s_demo_vfs);
    mel_io_shutdown(&s_demo_io);
    mel_vfs_backend_os_destroy(s_fonts_backend);
    mel_gpu_texture_shutdown(&s_stone_texture, dev);
    mel_gpu_texture_shutdown(&s_grass_texture, dev);
    mel_gpu_texture_shutdown(&s_white_texture, dev);
    mel_gpu_pipeline_shutdown(&s_pipeline, dev);
    mel_gpu_shader_shutdown(&s_shader, dev);
}

static void app_update(Mel_App* app, f32 dt)
{
    MEL_UNUSED(app);

    if (s_pf.search_state == SEARCH_RUNNING)
        mel_coro_update(s_pf.coro, dt);
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

    pathfinder_draw_tiles(&s_pf, &s_batch, &e->dev);
    pathfinder_draw_overlays(&s_pf, &s_batch, &e->dev);
    pathfinder_draw_grid_lines(&s_batch, &e->dev);

    Mel_Font_Atlas_Entry* fe = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (fe)
        pathfinder_draw_text(&s_pf, &s_batch, &e->dev, fe);

    mel_sprite_batch_end(&s_batch, &e->dev, c->cmd, &proj);
    mel_engine_end_swapchain_pass(e, c);
}

static GridPos screen_to_grid(f32 sx, f32 sy)
{
    GridPos p;
    p.x = (i32)((sx - GRID_X_OFFSET) / CELL_SIZE);
    p.y = (i32)((sy - GRID_Y_OFFSET) / CELL_SIZE);
    return p;
}

static bool grid_in_bounds(GridPos p)
{
    return p.x >= 0 && p.x < GRID_W && p.y >= 0 && p.y < GRID_H;
}

static void handle_left_click(f32 mx, f32 my)
{
    GridPos p = screen_to_grid(mx, my);
    if (!grid_in_bounds(p)) return;
    if (s_pf.search_state == SEARCH_RUNNING) return;

    if (s_pf.cells[p.y][p.x] == CELL_EMPTY)
        s_pf.cells[p.y][p.x] = CELL_WALL;
    else
        s_pf.cells[p.y][p.x] = CELL_EMPTY;

    if (s_pf.search_state != SEARCH_NONE)
        pathfinder_reset_search(&s_pf);
}

static void handle_right_click(f32 mx, f32 my)
{
    GridPos p = screen_to_grid(mx, my);
    if (!grid_in_bounds(p)) return;
    if (s_pf.search_state == SEARCH_RUNNING) return;
    if (s_pf.cells[p.y][p.x] == CELL_WALL) return;

    if (s_pf.right_click_is_start)
    {
        s_pf.start = p;
        s_pf.has_start = true;
    }
    else
    {
        s_pf.end = p;
        s_pf.has_end = true;
    }
    s_pf.right_click_is_start = !s_pf.right_click_is_start;

    if (s_pf.search_state != SEARCH_NONE)
        pathfinder_reset_search(&s_pf);
}

static void start_search(void)
{
    if (!s_pf.has_start || !s_pf.has_end) return;
    if (s_pf.search_state == SEARCH_RUNNING) return;

    pathfinder_reset_search(&s_pf);
    mel_coro_invoke(s_pf.coro, astar_solve, &s_pf);
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
            case SDL_SCANCODE_SPACE: start_search(); break;
            case SDL_SCANCODE_C:     pathfinder_clear(&s_pf); break;
            case SDL_SCANCODE_R:     pathfinder_reset_search(&s_pf); break;
            default: break;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if (event->button.button == SDL_BUTTON_LEFT)
            handle_left_click(event->button.x, event->button.y);
        else if (event->button.button == SDL_BUTTON_RIGHT)
            handle_right_click(event->button.x, event->button.y);
    }
}

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_update = app_update,
    .on_render = app_render,
    .on_event = app_event
)
