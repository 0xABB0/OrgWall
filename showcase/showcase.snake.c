#include "melody.h"

#define GW   20
#define GH   15
#define CELL 32
#define MAX_QUADS 512

#define ACT_UP      1
#define ACT_DOWN    2
#define ACT_LEFT    3
#define ACT_RIGHT   4
#define ACT_RESTART 5
#define ACT_QUIT    6

static const Mel_Vec4 COL_BG   = {0.11f, 0.11f, 0.14f, 1.0f};
static const Mel_Vec4 COL_GRID = {0.18f, 0.18f, 0.22f, 1.0f};
static const Mel_Vec4 COL_HEAD = {0.20f, 0.90f, 0.40f, 1.0f};
static const Mel_Vec4 COL_BODY = {0.15f, 0.70f, 0.30f, 1.0f};
static const Mel_Vec4 COL_FOOD = {0.95f, 0.30f, 0.30f, 1.0f};
static const Mel_Vec4 COL_DEAD = {0.90f, 0.20f, 0.20f, 1.0f};

typedef struct { i32 x, y; } Cell;
typedef struct { Mel_Vec2 pos; Mel_Vec4 col; } Quad_Vert;

typedef struct {
    Mel_Input input;
    Mel_Rng rng;
    Cell snake[GW * GH];
    Cell food, dir, queued;
    i32 len, score;
    bool dead;
    Mel_Gpu_Pipeline pipeline;
    Mel_Gpu_Buffer vbo;
    Mel_Gpu_Buffer ibo;
    Quad_Vert verts[MAX_QUADS * 4];
    u32 quad_count;
} Snake;

static void push_quad(Snake* s, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 col)
{
    assert(s->quad_count < MAX_QUADS);
    Quad_Vert* v = &s->verts[s->quad_count * 4];
    v[0] = (Quad_Vert){{x,     y    }, col};
    v[1] = (Quad_Vert){{x + w, y    }, col};
    v[2] = (Quad_Vert){{x + w, y + h}, col};
    v[3] = (Quad_Vert){{x,     y + h}, col};
    s->quad_count++;
}

static bool occupied(Snake* s, Cell c)
{
    for (i32 i = 0; i < s->len; i++)
        if (s->snake[i].x == c.x && s->snake[i].y == c.y) return true;
    return false;
}

static void spawn_food(Snake* s)
{
    do s->food = (Cell){mel_rng_range(&s->rng, 0, GW), mel_rng_range(&s->rng, 0, GH)};
    while (occupied(s, s->food));
}

static void new_game(Snake* s)
{
    s->len = 3;
    s->snake[0] = (Cell){GW / 2, GH / 2};
    for (i32 i = 1; i < s->len; i++) s->snake[i] = (Cell){s->snake[0].x - i, s->snake[0].y};
    s->dir = s->queued = (Cell){1, 0};
    s->score = 0;
    s->dead = false;
    spawn_food(s);
}

static void snake_tick(Mel_Sim sim, void* user)
{
    (void)user;
    Snake* s = mel_sim_get_state(sim);

    if (mel_action_pressed(s->input, ACT_UP)    && s->dir.y !=  1) s->queued = (Cell){ 0, -1};
    if (mel_action_pressed(s->input, ACT_DOWN)   && s->dir.y != -1) s->queued = (Cell){ 0,  1};
    if (mel_action_pressed(s->input, ACT_LEFT)   && s->dir.x !=  1) s->queued = (Cell){-1,  0};
    if (mel_action_pressed(s->input, ACT_RIGHT)  && s->dir.x != -1) s->queued = (Cell){ 1,  0};
    if (mel_action_pressed(s->input, ACT_RESTART)) { new_game(s); return; }
    if (mel_action_pressed(s->input, ACT_QUIT))    { mel_quit(); return; }

    if (s->dead) return;

    s->dir = s->queued;
    Cell next = {s->snake[0].x + s->dir.x, s->snake[0].y + s->dir.y};

    if (next.x < 0 || next.x >= GW || next.y < 0 || next.y >= GH || occupied(s, next)) {
        s->dead = true;
        return;
    }

    bool ate = (next.x == s->food.x && next.y == s->food.y);
    for (i32 i = ate ? s->len++ : s->len - 1; i > 0; i--) s->snake[i] = s->snake[i - 1];
    s->snake[0] = next;
    if (ate) { s->score += 10; spawn_food(s); }
}

static void snake_render(Mel_Pass_Context* ctx, void* user)
{
    Snake* s = user;
    s->quad_count = 0;

    for (i32 x = 0; x <= GW; x++)
        push_quad(s, (f32)(x * CELL), 0, 1, (f32)(GH * CELL), COL_GRID);
    for (i32 y = 0; y <= GH; y++)
        push_quad(s, 0, (f32)(y * CELL), (f32)(GW * CELL), 1, COL_GRID);

    for (i32 i = 0; i < s->len; i++) {
        f32 p = i == 0 ? 1.0f : 2.0f;
        Mel_Vec4 col = s->dead ? COL_DEAD : (i == 0 ? COL_HEAD : COL_BODY);
        push_quad(s, (f32)(s->snake[i].x * CELL) + p, (f32)(s->snake[i].y * CELL) + p,
                  (f32)CELL - p * 2, (f32)CELL - p * 2, col);
    }

    push_quad(s, (f32)(s->food.x * CELL) + 4, (f32)(s->food.y * CELL) + 4,
              (f32)CELL - 8, (f32)CELL - 8, COL_FOOD);

    mel_gpu_buffer_upload(&s->vbo, ctx->dev, s->verts, s->quad_count * 4 * sizeof(Quad_Vert), 0);

    Mel_Mat4 proj = mel_mat4_ortho(0, GW * CELL, GH * CELL, 0, -1, 1);
    mel_gpu_cmd_bind_pipeline(ctx->cmd, &s->pipeline);
    mel_gpu_cmd_bind_vertex_buffer(ctx->cmd, &s->vbo, 0);
    mel_gpu_cmd_bind_index_buffer(ctx->cmd, &s->ibo, 0, MEL_GPU_INDEX_TYPE_U16);
    mel_gpu_cmd_push_constants(ctx->cmd, &s->pipeline,
        MEL_GPU_SHADER_STAGE_VERTEX, 0, sizeof(Mel_Mat4), &proj);
    mel_gpu_cmd_draw_indexed(ctx->cmd, s->quad_count * 6, 1, 0, 0, 0);
}

static void snake_stage_init(Mel_Stage stage, void* user)
{
    (void)stage;
    Mel_Window win = user;
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Snake* s = mel_alloc(mel_allocator(), sizeof(Snake));
    *s = (Snake){0};

    Mel_Gpu_Shader shader;
    mel_gpu_shader_load(&shader, .path = S8("shaders/flat2d.slang"), .dev = dev);

    Mel_Gpu_Vertex_Binding vb = {.binding = 0, .stride = sizeof(Quad_Vert), .input_rate = 0};
    Mel_Gpu_Vertex_Attribute va[] = {
        {.location = 0, .binding = 0, .format = MEL_GPU_FORMAT_R32G32_SFLOAT,       .offset = offsetof(Quad_Vert, pos)},
        {.location = 1, .binding = 0, .format = MEL_GPU_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Quad_Vert, col)},
    };

    mel_gpu_pipeline_init(&s->pipeline, dev,
        .shader = &shader,
        .bindings = &vb,
        .binding_count = 1,
        .attributes = va,
        .attribute_count = 2,
        .color_format = mel_window_swapchain_format(win),
        .blend_mode = MEL_GPU_BLEND_ALPHA,
        .push_constant_size = sizeof(Mel_Mat4),
        .push_constant_stages = MEL_GPU_SHADER_STAGE_VERTEX,
    );

    mel_gpu_buffer_init(&s->vbo, dev,
        .size = sizeof(s->verts),
        .usage = MEL_GPU_BUFFER_USAGE_VERTEX | MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
        .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU);

    u16 indices[MAX_QUADS * 6];
    for (i32 i = 0; i < MAX_QUADS; i++) {
        indices[i * 6 + 0] = (u16)(i * 4 + 0);
        indices[i * 6 + 1] = (u16)(i * 4 + 1);
        indices[i * 6 + 2] = (u16)(i * 4 + 2);
        indices[i * 6 + 3] = (u16)(i * 4 + 2);
        indices[i * 6 + 4] = (u16)(i * 4 + 3);
        indices[i * 6 + 5] = (u16)(i * 4 + 0);
    }
    mel_gpu_buffer_init(&s->ibo, dev,
        .size = sizeof(indices),
        .usage = MEL_GPU_BUFFER_USAGE_INDEX | MEL_GPU_BUFFER_USAGE_TRANSFER_DST,
        .memory_usage = MEL_GPU_MEMORY_USAGE_GPU_ONLY);
    mel_gpu_buffer_upload(&s->ibo, dev, indices, sizeof(indices), 0);

    Mel_Graph graph = mel_graph();
    Mel_Render_Resource frame = mel_window_backbuffer(win, graph);
    frame = mel_pass(graph, snake_render, s, .colors = {{
        .image = frame,
        .load_op = MEL_GPU_LOAD_OP_CLEAR,
        .store_op = MEL_GPU_STORE_OP_STORE,
        .clear_r = COL_BG.x, .clear_g = COL_BG.y, .clear_b = COL_BG.z, .clear_a = COL_BG.w,
    }});
    (void)frame;

    Mel_Input in = mel_input_create();
    mel_input_bind(in, MEL_KEY_UP,     ACT_UP);
    mel_input_bind(in, MEL_KEY_DOWN,   ACT_DOWN);
    mel_input_bind(in, MEL_KEY_LEFT,   ACT_LEFT);
    mel_input_bind(in, MEL_KEY_RIGHT,  ACT_RIGHT);
    mel_input_bind(in, MEL_KEY_R,      ACT_RESTART);
    mel_input_bind(in, MEL_KEY_ESCAPE, ACT_QUIT);
    s->input = in;

    Mel_Input_Layer layer = mel_input_layer_create();
    mel_input_layer_attach(layer, in);

    mel_rng_seed(&s->rng, 0);
    Mel_Sim sim = mel_sim();
    mel_sim_set_state(sim, s);
    mel_sim_add_fixed(sim, snake_tick, 1.0f / 8, s);
    new_game(s);
    mel_sim_start(sim);
}

void app_init(void)
{
    Mel_Window win = mel_window(.title = S8("Snake"), .width = GW * CELL, .height = GH * CELL);
    mel_stage_add(mel_stage(.init = snake_stage_init, .user = win));
    mel_window_show(win);
}
