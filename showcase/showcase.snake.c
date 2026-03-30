#include "melody.h"

#define GW   20
#define GH   15
#define CELL 32

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
static const Mel_Vec4 COL_TEXT = {1.0f,  1.0f,  1.0f,  1.0f};
static const Mel_Vec4 COL_DEAD = {0.90f, 0.20f, 0.20f, 1.0f};

typedef struct { i32 x, y; } Cell;

typedef struct {
    Mel_Input input;
    Mel_Rng rng;
    Cell snake[GW * GH];
    Cell food, dir, queued;
    i32 len, score;
    bool dead;
} Snake;

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
    Snake* s = mel_sim_get_state(sim);

    if (mel_action_pressed(s->input, ACT_UP)      && s->dir.y !=  1) s->queued = (Cell){ 0, -1};
    if (mel_action_pressed(s->input, ACT_DOWN)    && s->dir.y != -1) s->queued = (Cell){ 0,  1};
    if (mel_action_pressed(s->input, ACT_LEFT)    && s->dir.x !=  1) s->queued = (Cell){-1,  0};
    if (mel_action_pressed(s->input, ACT_RIGHT)   && s->dir.x != -1) s->queued = (Cell){ 1,  0};
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

static void snake_draw(Mel_Sim sim, void* user)
{
    Snake* s = mel_sim_get_state(sim);
    Mel_Canvas cv = user;

    for (i32 i = 0; i < s->len; i++) {
        i32 p = i == 0 ? 1 : 2;
        mel_canvas_rect(cv, s->snake[i].x * CELL + p, s->snake[i].y * CELL + p,
                        CELL - p * 2, CELL - p * 2, i == 0 ? COL_HEAD : COL_BODY);
    }

    mel_canvas_rect(cv, s->food.x * CELL + 4, s->food.y * CELL + 4, CELL - 8, CELL - 8, COL_FOOD);
    mel_canvas_printf(cv, 4, 4, COL_TEXT, "Score: %d", s->score);
    if (s->dead)
        mel_canvas_printf(cv, GW * CELL / 2 - 80, GH * CELL / 2, COL_DEAD, "GAME OVER [R] Restart");
}

static void snake_stage_init(Mel_Stage stage, void* user)
{
    Mel_Window win = user;
    Mel_Target target = mel_target_window(win);
    Mel_View   view = mel_view(.projection = mel_mat4_ortho(0, GW * CELL, GH * CELL, 0, -1, 1));
  
    Mel_Canvas grid = mel_canvas();
    mel_canvas_rect(grid, 0, 0, GW * CELL, GH * CELL, COL_BG);
    for (i32 x = 0; x <= GW; x++)
        mel_canvas_line(grid, x * CELL, 0, x * CELL, GH * CELL, COL_GRID);
    for (i32 y = 0; y <= GH; y++)
        mel_canvas_line(grid, 0, y * CELL, GW * CELL, y * CELL, COL_GRID);
    mel_canvas_commit(grid);
  
    Snake* state = mel_alloc(mel_allocator(), sizeof(Snake));
    mel_rng_seed(&state->rng, 0);

    Mel_Canvas game = mel_canvas();

    mel_route(mel_source_canvas(grid), view, target);
    mel_route(mel_source_canvas(game), view, target);

    Mel_Input in = mel_input_create();
    mel_input_bind(in, MEL_KEY_UP,     ACT_UP);
    mel_input_bind(in, MEL_KEY_DOWN,   ACT_DOWN);
    mel_input_bind(in, MEL_KEY_LEFT,   ACT_LEFT);
    mel_input_bind(in, MEL_KEY_RIGHT,  ACT_RIGHT);
    mel_input_bind(in, MEL_KEY_R,      ACT_RESTART);
    mel_input_bind(in, MEL_KEY_ESCAPE, ACT_QUIT);
    state->input = in;

    Mel_Input_Layer in_layer = mel_input_layer_add();
    mel_input_layer_add(in_layer, in);

    Mel_Sim sim = mel_sim();
    mel_sim_set_state(sim, state);
    mel_sim_add_fixed(sim, snake_tick, 1.0f / 8, state);
    mel_sim_add_variable(sim, snake_draw, game);
    new_game(state);
  
    mel_sim_start(sim);
  
}

void app_init(void)
{
    Mel_Window win = mel_window(.title = S8("Snake"), .width = GW * CELL, .height = GH * CELL);

    Mel_Stage game_stage = mel_stage(.init = snake_stage_init, .user = win);
    mel_stage_add(game_stage);

    mel_window_show(win);
}
