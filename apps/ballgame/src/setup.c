#include <math.h>

#include <allocator/heap.h>
#include <boot/boot.h>
#include <gui/gui.h>
#include <string/str8.h>
#include <time/frame_clock.h>
#include <vat/tick.h>
#include <vat/vat.h>

#define TICK_NS     ((i64)16666667)
#define BALL_RADIUS 24.0f
#define BALL_SPEED  420.0f

typedef struct
{
    Mel_Vat*        vat;
    Mel_Vat_Tick*   timer;
    Mel_Gui_Handle  canvas;
    Mel_Frame_Clock clock;
    f32             x, y;
    f32             view_w, view_h;
    bool            up, left, down, right;
    bool            dragging;
    f32             drag_x, drag_y;
    bool            placed;
} Ball_Game;

static Ball_Game g;

static void key_state(Mel_Key key, bool held)
{
    if (key == MEL_KEY_W)
        g.up = held;
    if (key == MEL_KEY_A)
        g.left = held;
    if (key == MEL_KEY_S)
        g.down = held;
    if (key == MEL_KEY_D)
        g.right = held;
}

static void on_key_down(Mel_Gui_Handle h, Mel_Key key, void* user)
{
    (void)h;
    (void)user;
    key_state(key, true);
}

static void on_key_up(Mel_Gui_Handle h, Mel_Key key, void* user)
{
    (void)h;
    (void)user;
    key_state(key, false);
}

static void on_pointer_down(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)h;
    (void)user;
    g.dragging = true;
    g.drag_x = (f32)x;
    g.drag_y = (f32)y;
}

static void on_pointer_move(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)h;
    (void)user;
    if (!g.dragging)
        return;
    g.drag_x = (f32)x;
    g.drag_y = (f32)y;
}

static void on_pointer_up(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)h;
    (void)x;
    (void)y;
    (void)user;
    g.dragging = false;
}

static void clamp_to_view(void)
{
    if (g.view_w <= 0 || g.view_h <= 0)
        return;
    if (g.x < BALL_RADIUS)
        g.x = BALL_RADIUS;
    if (g.y < BALL_RADIUS)
        g.y = BALL_RADIUS;
    if (g.x > g.view_w - BALL_RADIUS)
        g.x = g.view_w - BALL_RADIUS;
    if (g.y > g.view_h - BALL_RADIUS)
        g.y = g.view_h - BALL_RADIUS;
}

static bool tick(void* user)
{
    (void)user;
    f32 dt = (f32)mel_dur_as_secs_f64(mel_frame_clock_tick(&g.clock));
    if (g.placed)
    {
        f32 dx = (f32)((g.right ? 1 : 0) - (g.left ? 1 : 0));
        f32 dy = (f32)((g.down ? 1 : 0) - (g.up ? 1 : 0));
        if (g.dragging)
        {
            dx = g.drag_x - g.x;
            dy = g.drag_y - g.y;
        }
        f32 len = sqrtf(dx * dx + dy * dy);
        if (len > 0.0f)
        {
            f32 step = BALL_SPEED * dt;
            if (g.dragging && step > len)
                step = len;
            g.x += dx / len * step;
            g.y += dy / len * step;
            clamp_to_view();
        }
    }
    if (!mel_gui_handle_is_none(g.canvas))
        mel_gui_invalidate(g.canvas);
    return true;
}

static void canvas_paint(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 view_h, void* user)
{
    (void)h;
    (void)user;
    g.view_w = (f32)w;
    g.view_h = (f32)view_h;
    if (!g.placed)
    {
        g.x = g.view_w * 0.5f;
        g.y = g.view_h * 0.5f;
        g.placed = true;
    }
    mel_painter_clear(p, mel_color8_rgb(18, 22, 30));
    mel_painter_fill_ellipse(p, mel_rect(g.x - BALL_RADIUS, g.y - BALL_RADIUS, BALL_RADIUS * 2, BALL_RADIUS * 2), mel_color8_rgb(255, 120, 60));
    mel_painter_draw_text(p, S8("WASD to move — drag to steer"), mel_vec2(12, 12), mel_color8_rgb(150, 165, 185), 12.0f);
}

static void build_game(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    mel_gui_set_text(frame, S8("Ball Game"));
    mel_gui_set_layout(frame, mel_column_layout(.cross_align = MEL_ALIGN_STRETCH));
    g.canvas = mel_canvas_create(frame,
                                 .on_.on_paint = canvas_paint,
                                 .keyboard = { .on_key_down = on_key_down, .on_key_up = on_key_up },
                                 .pointer = { .on_pointer_down = on_pointer_down, .on_pointer_move = on_pointer_move, .on_pointer_up = on_pointer_up },
                                 .layoutable = { .preferred_w = 800, .preferred_h = 600, .weight = 1 });
    mel_gui_set_focus(g.canvas);
    mel_frame_clock_init(&g.clock, mel_dur_ms(100), 0.0);
    g.timer = mel_vat_tick_open(g.vat, mel_alloc_heap(), TICK_NS, tick, NULL);
}

void mel_app_setup(Mel_Vat* root)
{
    g.canvas = MEL_GUI_HANDLE_NONE;
    g.vat = root;
    mel_gui_init(root);
    mel_app_register_screen(S8("game"), build_game, NULL);
    mel_app_present(S8("game"), NULL);
}
