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
#include "gpu.buffer.h"
#include "gpu.cmd.h"
#include "render.draw.h"
#include "texture.pool.h"
#include "render.graph.h"
#include "render.list.h"
#include "render.target.h"
#include "render.sync.h"
#include "sprite.pass.h"
#include "render.camera.h"
#include "font.atlas.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "ecs.world.h"
#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"
#include "sim.ctx.h"

#define GRID_W 20
#define GRID_H 20
#define CELL_SIZE 24.0f
#define GRID_X_OFFSET 40.0f
#define GRID_Y_OFFSET 60.0f
#define INFO_X (GRID_X_OFFSET + (GRID_W + 1) * CELL_SIZE)

typedef struct { i32 gx; i32 gy; } Snake_CSegment;
typedef struct { i32 dx; i32 dy; } Snake_CDirection;
typedef struct { ecs_entity_t target; i32 prev_x; i32 prev_y; } Snake_CFollows;

ECS_COMPONENT_DECLARE(Snake_CSegment);
ECS_COMPONENT_DECLARE(Snake_CDirection);
ECS_COMPONENT_DECLARE(Snake_CFollows);
ECS_TAG_DECLARE(Snake_CHead);
ECS_TAG_DECLARE(Snake_CFood);

typedef struct {
    Mel_ECS ecs;
    ecs_entity_t head;
    ecs_entity_t tail;
    ecs_entity_t food;
    ecs_query_t* segment_query;
    ecs_query_t* follow_query;
    i32 queued_dx;
    i32 queued_dy;
    f32 move_timer;
    f32 move_interval;
    u32 score;
    u32 length;
    bool game_over;
    u32 rng_state;
} Snake;

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Sprite_Pass* s_sp;
static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Io s_demo_io;
static Mel_Vfs s_demo_vfs;
static Mel_Vfs_Backend* s_fonts_backend;
static Mel_Font_Handle s_font_handle;
static Snake s_snake;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static Mel_Render_Sync s_sync;
static Mel_Draw_Ctx s_grid_ctx;
static Mel_Render_List s_sprite_list;
static Mel_Render_Target s_swapchain_target;
static Mel_Render_Graph s_graph;
static Mel_Camera s_camera;

static u32 snake_rand(Snake* s)
{
    s->rng_state ^= s->rng_state << 13;
    s->rng_state ^= s->rng_state >> 17;
    s->rng_state ^= s->rng_state << 5;
    return s->rng_state;
}

static bool grid_occupied(Snake* s, i32 gx, i32 gy)
{
    ecs_iter_t it = ecs_query_iter(s->ecs.world, s->segment_query);
    while (ecs_query_next(&it))
    {
        Snake_CSegment* seg = ecs_field(&it, Snake_CSegment, 0);
        for (int i = 0; i < it.count; i++)
            if (seg[i].gx == gx && seg[i].gy == gy) { ecs_iter_fini(&it); return true; }
    }
    return false;
}

static ecs_entity_t create_segment(ecs_world_t* world, i32 gx, i32 gy, Mel_Vec4 color)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Snake_CSegment, { .gx = gx, .gy = gy });
    f32 pad = 1.0f;
    ecs_set(world, e, Mel_CTransform, {
        .pos = mel_vec2(GRID_X_OFFSET + (f32)gx * CELL_SIZE + pad,
                        GRID_Y_OFFSET + (f32)gy * CELL_SIZE + pad),
    });
    ecs_set(world, e, Mel_Sprite, {
        .size = mel_vec2(CELL_SIZE - pad * 2, CELL_SIZE - pad * 2),
        .color = color,
    });
    return e;
}

static void spawn_food(Snake* s)
{
    ecs_world_t* world = s->ecs.world;

    i32 fx, fy;
    do {
        fx = (i32)(snake_rand(s) % GRID_W);
        fy = (i32)(snake_rand(s) % GRID_H);
    } while (grid_occupied(s, fx, fy));

    if (s->food && ecs_is_valid(world, s->food))
        ecs_delete(world, s->food);

    s->food = ecs_new(world);
    ecs_set(world, s->food, Snake_CSegment, { .gx = fx, .gy = fy });
    ecs_add(world, s->food, Snake_CFood);
    f32 pad = 1.0f;
    ecs_set(world, s->food, Mel_CTransform, {
        .pos = mel_vec2(GRID_X_OFFSET + (f32)fx * CELL_SIZE + pad,
                        GRID_Y_OFFSET + (f32)fy * CELL_SIZE + pad),
    });
    ecs_set(world, s->food, Mel_Sprite, {
        .size = mel_vec2(CELL_SIZE - pad * 2, CELL_SIZE - pad * 2),
        .color = mel_vec4(0.9f, 0.2f, 0.2f, 1.0f),
    });
}

static void snake_init(Snake* s)
{
    if (s->segment_query)
    {
        ecs_query_fini(s->segment_query);
        s->segment_query = nullptr;
    }
    if (s->follow_query)
    {
        ecs_query_fini(s->follow_query);
        s->follow_query = nullptr;
    }
    if (s->ecs.world)
        mel_ecs_shutdown(&s->ecs);

    *s = (Snake){0};
    s->rng_state = (u32)SDL_GetTicks();
    if (s->rng_state == 0) s->rng_state = 42;
    s->move_interval = 0.12f;

    mel_ecs_init(&s->ecs);
    ecs_world_t* world = s->ecs.world;

    ECS_COMPONENT_DEFINE(world, Snake_CSegment);
    ECS_COMPONENT_DEFINE(world, Snake_CDirection);
    ECS_COMPONENT_DEFINE(world, Snake_CFollows);
    ECS_TAG_DEFINE(world, Snake_CHead);
    ECS_TAG_DEFINE(world, Snake_CFood);

    mel_component_transform_register(world);
    mel_component_sprite_register(world);

    s->segment_query = ecs_query(world, {
        .terms = { { .id = ecs_id(Snake_CSegment) } }
    });

    s->follow_query = ecs_query(world, {
        .terms = {
            { .id = ecs_id(Snake_CSegment) },
            { .id = ecs_id(Snake_CFollows) }
        }
    });

    i32 start_x = GRID_W / 2;
    i32 start_y = GRID_H / 2;

    Mel_Vec4 head_color = mel_vec4(0.2f, 0.9f, 0.2f, 1.0f);
    Mel_Vec4 body_color = mel_vec4(0.1f, 0.7f, 0.1f, 1.0f);

    s->head = create_segment(world, start_x, start_y, head_color);
    ecs_add(world, s->head, Snake_CHead);
    ecs_set(world, s->head, Snake_CDirection, { .dx = 1, .dy = 0 });

    ecs_entity_t prev = s->head;
    for (i32 i = 1; i < 3; i++)
    {
        ecs_entity_t seg = create_segment(world, start_x - i, start_y, body_color);
        ecs_set(world, seg, Snake_CFollows, {
            .target = prev,
            .prev_x = start_x - i + 1,
            .prev_y = start_y
        });
        prev = seg;
    }
    s->tail = prev;
    s->length = 3;
    s->queued_dx = 1;
    s->queued_dy = 0;

    spawn_food(s);
}

static void snake_grow(Snake* s)
{
    ecs_world_t* world = s->ecs.world;

    const Snake_CSegment* tail_seg = ecs_get(world, s->tail, Snake_CSegment);
    const Snake_CFollows* tail_follow = ecs_get(world, s->tail, Snake_CFollows);

    i32 new_x = tail_seg->gx;
    i32 new_y = tail_seg->gy;
    if (tail_follow)
    {
        new_x = 2 * tail_seg->gx - tail_follow->prev_x;
        new_y = 2 * tail_seg->gy - tail_follow->prev_y;
        new_x = (new_x % GRID_W + GRID_W) % GRID_W;
        new_y = (new_y % GRID_H + GRID_H) % GRID_H;
    }

    Mel_Vec4 body_color = mel_vec4(0.1f, 0.7f, 0.1f, 1.0f);
    ecs_entity_t seg = create_segment(world, new_x, new_y, body_color);
    ecs_set(world, seg, Snake_CFollows, {
        .target = s->tail,
        .prev_x = tail_seg->gx,
        .prev_y = tail_seg->gy
    });
    s->tail = seg;
    s->length++;
}

static void snake_tick(Snake* s, f32 dt)
{
    if (s->game_over) return;

    s->move_timer += dt;
    if (s->move_timer < s->move_interval) return;
    s->move_timer -= s->move_interval;

    ecs_world_t* world = s->ecs.world;

    Snake_CDirection* dir = ecs_get_mut(world, s->head, Snake_CDirection);
    dir->dx = s->queued_dx;
    dir->dy = s->queued_dy;

    ecs_iter_t fit = ecs_query_iter(world, s->follow_query);
    while (ecs_query_next(&fit))
    {
        Snake_CFollows* fol = ecs_field(&fit, Snake_CFollows, 1);

        for (int i = 0; i < fit.count; i++)
        {
            const Snake_CSegment* target_seg = ecs_get(world, fol[i].target, Snake_CSegment);
            fol[i].prev_x = target_seg->gx;
            fol[i].prev_y = target_seg->gy;
        }
    }

    const Snake_CSegment* head_seg = ecs_get(world, s->head, Snake_CSegment);
    i32 next_x = head_seg->gx + dir->dx;
    i32 next_y = head_seg->gy + dir->dy;

    if (next_x < 0 || next_x >= GRID_W || next_y < 0 || next_y >= GRID_H)
    {
        s->game_over = true;
        return;
    }

    ecs_iter_t cit = ecs_query_iter(world, s->follow_query);
    while (ecs_query_next(&cit))
    {
        Snake_CSegment* seg = ecs_field(&cit, Snake_CSegment, 0);
        for (int i = 0; i < cit.count; i++)
        {
            if (seg[i].gx == next_x && seg[i].gy == next_y)
            {
                s->game_over = true;
                ecs_iter_fini(&cit);
                return;
            }
        }
    }

    Snake_CSegment* head_mut = ecs_get_mut(world, s->head, Snake_CSegment);
    head_mut->gx = next_x;
    head_mut->gy = next_y;

    const Snake_CSegment* food_seg = ecs_get(world, s->food, Snake_CSegment);
    if (food_seg && next_x == food_seg->gx && next_y == food_seg->gy)
    {
        s->score += 10;
        snake_grow(s);
        spawn_food(s);

        if (s->move_interval > 0.05f)
            s->move_interval -= 0.002f;
    }

    ecs_iter_t mit = ecs_query_iter(world, s->follow_query);
    while (ecs_query_next(&mit))
    {
        Snake_CSegment* seg = ecs_field(&mit, Snake_CSegment, 0);
        Snake_CFollows* fol = ecs_field(&mit, Snake_CFollows, 1);

        for (int i = 0; i < mit.count; i++)
        {
            seg[i].gx = fol[i].prev_x;
            seg[i].gy = fol[i].prev_y;
        }
    }
}

static void snake_sync_transforms(Snake* s)
{
    ecs_world_t* world = s->ecs.world;
    ecs_iter_t it = ecs_query_iter(world, s->segment_query);
    while (ecs_query_next(&it))
    {
        Snake_CSegment* seg = ecs_field(&it, Snake_CSegment, 0);
        for (int i = 0; i < it.count; i++)
        {
            f32 pad = 1.0f;
            Mel_CTransform* t = ecs_get_mut(world, it.entities[i], Mel_CTransform);
            t->pos.x = GRID_X_OFFSET + (f32)seg[i].gx * CELL_SIZE + pad;
            t->pos.y = GRID_Y_OFFSET + (f32)seg[i].gy * CELL_SIZE + pad;
        }
    }
}

static void snake_draw_text(Snake* s, Mel_Draw_Ctx* ctx, Mel_Font_Atlas_Pool* pool, Mel_Font_Handle font)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.6f, 0.6f, 0.6f, 1.0f);

    char buf[64];

    mel_draw_ctx_text(ctx, .font = font, .text = S8("SNAKE"), .x = GRID_X_OFFSET, .y = 20.0f, .color = white);

    snprintf(buf, sizeof(buf), "SCORE: %u", s->score);
    mel_draw_ctx_text(ctx, .font = font, .text = str8_from_cstr(buf), .x = INFO_X, .y = GRID_Y_OFFSET, .color = white);

    snprintf(buf, sizeof(buf), "LENGTH: %u", s->length);
    mel_draw_ctx_text(ctx, .font = font, .text = str8_from_cstr(buf), .x = INFO_X, .y = GRID_Y_OFFSET + 40.0f, .color = white);

    if (s->game_over)
    {
        Mel_Vec4 red = mel_vec4(1.0f, 0.2f, 0.2f, 1.0f);
        f32 cx = GRID_X_OFFSET + 30.0f;
        f32 cy = GRID_Y_OFFSET + GRID_H * CELL_SIZE / 2.0f - 10.0f;
        mel_draw_ctx_text(ctx, .font = font, .text = S8("GAME OVER"), .x = cx, .y = cy, .color = red);
        mel_draw_ctx_text(ctx, .font = font, .text = S8("R to restart"), .x = cx + 10.0f, .y = cy + 30.0f, .color = dim);
    }
}

static void build_grid(Mel_Draw_Ctx* ctx)
{
    mel_draw_ctx_clear(ctx);

    mel_draw_ctx_rect(ctx, GRID_X_OFFSET, GRID_Y_OFFSET,
        (f32)GRID_W * CELL_SIZE, (f32)GRID_H * CELL_SIZE,
        mel_vec4(0.05f, 0.05f, 0.08f, 1.0f));

    Mel_Vec4 line_color = mel_vec4(0.1f, 0.1f, 0.13f, 1.0f);
    for (i32 row = 0; row <= GRID_H; row++)
    {
        f32 y = GRID_Y_OFFSET + (f32)row * CELL_SIZE;
        mel_draw_ctx_rect(ctx, GRID_X_OFFSET, y, (f32)GRID_W * CELL_SIZE, 1.0f, line_color);
    }
    for (i32 col = 0; col <= GRID_W; col++)
    {
        f32 x = GRID_X_OFFSET + (f32)col * CELL_SIZE;
        mel_draw_ctx_rect(ctx, x, GRID_Y_OFFSET, 1.0f, (f32)GRID_H * CELL_SIZE, line_color);
    }
}

static void snake_write_entry(void* entry_ptr, ecs_iter_t* it, i32 row, void* user)
{
    (void)user;
    Mel_CTransform* t = ecs_field(it, Mel_CTransform, 0);
    Mel_Sprite* sp = ecs_field(it, Mel_Sprite, 1);
    Mel_Sprite_Entry* entry = entry_ptr;
    entry->pos = t[row].pos;
    entry->size = sp[row].size;
    entry->color = sp[row].color;
    entry->tex = sp[row].tex;
    entry->uv = sp[row].uv.w > 0 ? sp[row].uv : MEL_UV_FULL;
}

static void grid_pass(Mel_Render_Pass_Ctx* ctx)
{
    Mel_Mat4 proj = mel_camera_vp(ctx->camera);
    mel_draw_ctx_render(&s_grid_ctx, ctx->cmd.cmd, &proj);
}

static void on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    s_sp = mel_sprite_pass();

    mel_draw_ctx_init(&s_grid_ctx,
        .pipeline = &s_sp->pipeline, .texture = &s_sp->white_texture,
        .pool = mel_texture_pool(), .font_pool = &s_font_pool, .dev = dev, .alloc = mel_alloc_heap());

    mel_font_atlas_pool_init(&s_font_pool, mel_allocator(), dev, &s_demo_vfs, .texture_pool = mel_texture_pool());
    s_font_handle = mel_font_atlas_pool_load(&s_font_pool,
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 18.0f);


    snake_init(&s_snake);

    mel_render_list_init(&s_sprite_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_sync_init(&s_sync,
        .list = &s_sprite_list,
        .world = s_snake.ecs.world,
        .components = { ecs_id(Mel_CTransform), ecs_id(Mel_Sprite) },
        .write = snake_write_entry);

    mel_render_target_init_swapchain(&s_swapchain_target, sc, dev, S8("backbuffer"));

    s_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)sc->extent.width,
                                      0, (f32)sc->extent.height, -1, 1),
    };

    mel_render_graph_init(&s_graph, .dev = dev, .alloc = mel_alloc_heap());

    u32 grid_id = mel_render_graph_add_pass(&s_graph, S8("grid"),
        .fn = grid_pass,
        .camera = &s_camera,
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.08f, .g = 0.08f, .b = 0.1f, .a = 1.0f } }));

    u32 sprite_id = mel_render_graph_add_pass(&s_graph, S8("sprite"),
        .fn = mel_sprite_pass_execute,
        .user = s_sp,
        .camera = &s_camera,
        .read_lists = MEL_LISTS(&s_sprite_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_LOAD }));

    mel_render_graph_pass_depends_on(&s_graph, sprite_id, grid_id);
    mel_render_graph_compile(&s_graph);
    mel_set_render_graph(&s_graph);

    SDL_Log("Snake ready! Arrow keys to move, R to restart, ESC to quit");
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

static void app_init(Mel_App* app)
{
    mel_init(.app_name = S8("Melody Snake"), .enable_validation = true);
    s_window_handle = mel_window_create(S8("Melody Snake"), .width = 640, .height = 580);
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

    mel_draw_ctx_shutdown(&s_grid_ctx);
    mel_render_sync_shutdown(&s_sync);
    mel_render_list_shutdown(&s_sprite_list);
    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_swapchain_target);

    if (s_snake.segment_query) ecs_query_fini(s_snake.segment_query);
    if (s_snake.follow_query) ecs_query_fini(s_snake.follow_query);
    s_snake.segment_query = nullptr;
    s_snake.follow_query = nullptr;
    mel_ecs_shutdown(&s_snake.ecs);

    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_vfs_unmount(&s_demo_vfs, S8("/"));
    mel_vfs_shutdown(&s_demo_vfs);
    mel_io_shutdown(&s_demo_io);
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);
    snake_tick(&s_snake, dt);
    snake_sync_transforms(&s_snake);

    mel_render_sync_update(&s_sync);

    build_grid(&s_grid_ctx);
    snake_draw_text(&s_snake, &s_grid_ctx, &s_font_pool, s_font_handle);
    mel_draw_ctx_commit(&s_grid_ctx);
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
        Snake* s = &s_snake;
        const Snake_CDirection* dir = ecs_get(s->ecs.world, s->head, Snake_CDirection);

        switch (event->key.scancode)
        {
            case SDL_SCANCODE_LEFT:
                if (dir && dir->dx != 1) { s->queued_dx = -1; s->queued_dy = 0; }
                break;
            case SDL_SCANCODE_RIGHT:
                if (dir && dir->dx != -1) { s->queued_dx = 1; s->queued_dy = 0; }
                break;
            case SDL_SCANCODE_UP:
                if (dir && dir->dy != 1) { s->queued_dx = 0; s->queued_dy = -1; }
                break;
            case SDL_SCANCODE_DOWN:
                if (dir && dir->dy != -1) { s->queued_dx = 0; s->queued_dy = 1; }
                break;
            case SDL_SCANCODE_R:
                mel_render_sync_shutdown(&s_sync);
                snake_init(s);
                mel_render_sync_init(&s_sync,
                    .list = &s_sprite_list,
                    .world = s->ecs.world,
                    .components = { ecs_id(Mel_CTransform), ecs_id(Mel_Sprite) },
                    .write = snake_write_entry);
                break;
            default: break;
        }
    }
}

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_event = app_event
)
