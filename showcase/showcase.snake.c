#include "melody.h"

#define GW   20
#define GH   15
#define CELL 32

static const Mel_Vec4 COL_BG   = {0.11f, 0.11f, 0.14f, 1.0f};
static const Mel_Vec4 COL_GRID = {0.18f, 0.18f, 0.22f, 1.0f};
static const Mel_Vec4 COL_HEAD = {0.20f, 0.90f, 0.40f, 1.0f};
static const Mel_Vec4 COL_BODY = {0.15f, 0.70f, 0.30f, 1.0f};
static const Mel_Vec4 COL_FOOD = {0.95f, 0.30f, 0.30f, 1.0f};
static const Mel_Vec4 COL_TEXT = {1.0f,  1.0f,  1.0f,  1.0f};
static const Mel_Vec4 COL_DEAD = {0.90f, 0.20f, 0.20f, 1.0f};

typedef struct { i32 x, y; } Cell;

static Mel_Canvas grid_cv;
static Mel_Canvas game_cv;
static Cell snake[GW * GH], food, dir, queued;
static i32  len, score;
static bool dead;
static Mel_Rng rng;

static bool occupied(Cell c)
{
    for (i32 i = 0; i < len; i++)
        if (snake[i].x == c.x && snake[i].y == c.y) return true;
    return false;
}

static void spawn_food(void)
{
    do food = (Cell){mel_rng_range(&rng, 0, GW), mel_rng_range(&rng, 0, GH)};
    while (occupied(food));
}

static void new_game(void)
{
    len = 3;
    snake[0] = (Cell){GW / 2, GH / 2};
    for (i32 i = 1; i < len; i++) snake[i] = (Cell){snake[0].x - i, snake[0].y};
    dir = queued = (Cell){1, 0};
    score = 0;
    dead = false;
    spawn_food();
}

static void snake_update(Mel_Sim* sim)
{
    for (Mel_Event* e = mel_sim_events(sim); e; e = e->next) {
        if (e->type != MEL_EVENT_KEY_DOWN) continue;
        switch (e->key) {
            case MEL_KEY_UP:    if (dir.y !=  1) queued = (Cell){ 0, -1}; break;
            case MEL_KEY_DOWN:  if (dir.y != -1) queued = (Cell){ 0,  1}; break;
            case MEL_KEY_LEFT:  if (dir.x !=  1) queued = (Cell){-1,  0}; break;
            case MEL_KEY_RIGHT: if (dir.x != -1) queued = (Cell){ 1,  0}; break;
            case MEL_KEY_R:     new_game(); return;
            case MEL_KEY_ESCAPE: mel_quit(); return;
        }
    }

    if (dead) return;

    dir = queued;
    Cell next = {snake[0].x + dir.x, snake[0].y + dir.y};

    if (next.x < 0 || next.x >= GW || next.y < 0 || next.y >= GH || occupied(next)) {
        dead = true;
        return;
    }

    bool ate = (next.x == food.x && next.y == food.y);
    for (i32 i = ate ? len++ : len - 1; i > 0; i--) snake[i] = snake[i - 1];
    snake[0] = next;
    if (ate) { score += 10; spawn_food(); }

    mel_canvas_clear(&game_cv);

    for (i32 i = 0; i < len; i++) {
        i32 p = i == 0 ? 1 : 2;
        mel_canvas_rect(&game_cv, snake[i].x * CELL + p, snake[i].y * CELL + p,
                        CELL - p * 2, CELL - p * 2, i == 0 ? COL_HEAD : COL_BODY);
    }

    mel_canvas_rect(&game_cv, food.x * CELL + 4, food.y * CELL + 4, CELL - 8, CELL - 8, COL_FOOD);

    mel_canvas_printf(&game_cv, 4, 4, COL_TEXT, "Score: %d", score);
    if (dead)
        mel_canvas_printf(&game_cv, GW * CELL / 2 - 80, GH * CELL / 2, COL_DEAD, "GAME OVER [R] Restart");

    mel_canvas_commit(&game_cv);
}

void app_init(void)
{
    Mel_Window win = mel_window(.title = S8("Snake"), .width = GW * CELL, .height = GH * CELL);

    grid_cv = mel_canvas(win, .layer = 0);
    game_cv = mel_canvas(win, .layer = 1);

    for (i32 x = 0; x <= GW; x++)
        mel_canvas_line(&grid_cv, x * CELL, 0, x * CELL, GH * CELL, COL_GRID);
    for (i32 y = 0; y <= GH; y++)
        mel_canvas_line(&grid_cv, 0, y * CELL, GW * CELL, y * CELL, COL_GRID);
    mel_canvas_commit(&grid_cv);

    mel_rng_seed(&rng, 0);
    new_game();

    mel_sim(.hz = 8, .update = snake_update);
}
