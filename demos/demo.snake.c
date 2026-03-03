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
#include "ecs.world.h"

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
static Snake s_snake;

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
}

static ecs_entity_t create_segment(ecs_world_t* world, i32 gx, i32 gy)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Snake_CSegment, { .gx = gx, .gy = gy });
    return e;
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

    s->head = create_segment(world, start_x, start_y);
    ecs_add(world, s->head, Snake_CHead);
    ecs_set(world, s->head, Snake_CDirection, { .dx = 1, .dy = 0 });

    ecs_entity_t prev = s->head;
    for (i32 i = 1; i < 3; i++)
    {
        ecs_entity_t seg = create_segment(world, start_x - i, start_y);
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

    ecs_entity_t seg = create_segment(world, new_x, new_y);
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

static void draw_grid_cell(Mel_SpriteBatch* batch, i32 gx, i32 gy, Mel_Vec4 color)
{
    f32 x = GRID_X_OFFSET + (f32)gx * CELL_SIZE;
    f32 y = GRID_Y_OFFSET + (f32)gy * CELL_SIZE;
    f32 pad = 1.0f;
    mel_sprite_batch_draw(batch, x + pad, y + pad, CELL_SIZE - pad * 2, CELL_SIZE - pad * 2, color);
}

static void snake_draw_grid(Snake* s, Mel_SpriteBatch* batch)
{
    Mel_Vec4 bg = mel_vec4(0.05f, 0.05f, 0.08f, 1.0f);
    mel_sprite_batch_draw(batch,
        GRID_X_OFFSET, GRID_Y_OFFSET,
        (f32)GRID_W * CELL_SIZE, (f32)GRID_H * CELL_SIZE, bg);

    Mel_Vec4 line_color = mel_vec4(0.1f, 0.1f, 0.13f, 1.0f);
    for (i32 row = 0; row <= GRID_H; row++)
    {
        f32 y = GRID_Y_OFFSET + (f32)row * CELL_SIZE;
        mel_sprite_batch_draw(batch, GRID_X_OFFSET, y, (f32)GRID_W * CELL_SIZE, 1.0f, line_color);
    }
    for (i32 col = 0; col <= GRID_W; col++)
    {
        f32 x = GRID_X_OFFSET + (f32)col * CELL_SIZE;
        mel_sprite_batch_draw(batch, x, GRID_Y_OFFSET, 1.0f, (f32)GRID_H * CELL_SIZE, line_color);
    }

    ecs_world_t* world = s->ecs.world;

    Mel_Vec4 head_color = mel_vec4(0.2f, 0.9f, 0.2f, 1.0f);
    Mel_Vec4 body_color = mel_vec4(0.1f, 0.7f, 0.1f, 1.0f);

    ecs_iter_t it = ecs_query_iter(world, s->segment_query);
    while (ecs_query_next(&it))
    {
        Snake_CSegment* seg = ecs_field(&it, Snake_CSegment, 0);
        for (int i = 0; i < it.count; i++)
        {
            if (ecs_has(world, it.entities[i], Snake_CFood)) continue;

            bool is_head = ecs_has(world, it.entities[i], Snake_CHead);
            draw_grid_cell(batch, seg[i].gx, seg[i].gy, is_head ? head_color : body_color);
        }
    }

    if (s->food && ecs_is_valid(world, s->food))
    {
        const Snake_CSegment* food_seg = ecs_get(world, s->food, Snake_CSegment);
        if (food_seg)
        {
            Mel_Vec4 food_color = mel_vec4(0.9f, 0.2f, 0.2f, 1.0f);
            draw_grid_cell(batch, food_seg->gx, food_seg->gy, food_color);
        }
    }
}

static void snake_draw_text(Snake* s, Mel_SpriteBatch* batch, Mel_Font_Atlas_Entry* fe)
{
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.6f, 0.6f, 0.6f, 1.0f);

    char buf[64];

    mel_font_atlas_draw_text(fe, batch, S8("SNAKE"), GRID_X_OFFSET, 20.0f, white);

    snprintf(buf, sizeof(buf), "SCORE: %u", s->score);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), INFO_X, GRID_Y_OFFSET, white);

    snprintf(buf, sizeof(buf), "LENGTH: %u", s->length);
    mel_font_atlas_draw_text(fe, batch, str8_from_cstr(buf), INFO_X, GRID_Y_OFFSET + 40.0f, white);

    if (s->game_over)
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

    snake_init(&s_snake);

    SDL_Log("Snake ready! Arrow keys to move, R to restart, ESC to quit");
}

static void app_init(Mel_App* app)
{
    s_window = SDL_CreateWindow("Melody Snake", 640, 580,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    assert(s_window);

    mel_engine_init(&app->engine,
        .window = s_window,
        .app_name = S8("Melody Snake"),
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

    if (s_snake.segment_query) ecs_query_fini(s_snake.segment_query);
    if (s_snake.follow_query) ecs_query_fini(s_snake.follow_query);
    s_snake.segment_query = nullptr;
    s_snake.follow_query = nullptr;
    mel_ecs_shutdown(&s_snake.ecs);

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
    snake_tick(&s_snake, dt);
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
    snake_draw_grid(&s_snake, &s_batch);

    Mel_Font_Atlas_Entry* fe = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (fe)
        snake_draw_text(&s_snake, &s_batch, fe);

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
                snake_init(s);
                break;
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
