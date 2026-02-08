#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

#include "../melody/ui.widget.h"
#include "../melody/math.vec4.h"

#define SCALE 2.0f
#define WIN_W 800
#define WIN_H 480
#define INT_W (WIN_W / SCALE)
#define INT_H (WIN_H / SCALE)

static SDL_Window* s_window;
static SDL_Renderer* s_renderer;

typedef struct {
    Mel_Widget base;
    Mel_Vec4 color;
    bool outline_only;
} DPanel;

typedef struct {
    Mel_Widget base;
    const char* text;
    Mel_Vec4 color;
} DLabel;

typedef void (*DButton_Click_Fn)(void* user_data);

typedef struct {
    Mel_Widget base;
    const char* text;
    Mel_Vec4 normal_color;
    Mel_Vec4 hover_color;
    Mel_Vec4 pressed_color;
    Mel_Vec4 text_color;
    Mel_Vec4 disabled_color;
    DButton_Click_Fn on_click;
    void* click_data;
} DButton;

static void dpanel_draw(Mel_Widget* w, void* ctx)
{
    DPanel* panel = (DPanel*)w;
    SDL_Renderer* r = (SDL_Renderer*)ctx;
    SDL_FRect rect = { w->pos.x, w->pos.y, w->size.x, w->size.y };
    SDL_SetRenderDrawColor(r,
        (u8)(panel->color.x * 255),
        (u8)(panel->color.y * 255),
        (u8)(panel->color.z * 255),
        (u8)(panel->color.w * 255));
    if (panel->outline_only)
        SDL_RenderRect(r, &rect);
    else
        SDL_RenderFillRect(r, &rect);
}

static void dpanel_init(DPanel* panel, Mel_Vec4 color)
{
    mel_widget_init(&panel->base);
    panel->base.draw = dpanel_draw;
    panel->color = color;
    panel->outline_only = false;
}

static void dlabel_draw(Mel_Widget* w, void* ctx)
{
    DLabel* label = (DLabel*)w;
    if (!label->text || !label->text[0]) return;
    SDL_Renderer* r = (SDL_Renderer*)ctx;
    SDL_SetRenderDrawColor(r,
        (u8)(label->color.x * 255),
        (u8)(label->color.y * 255),
        (u8)(label->color.z * 255),
        (u8)(label->color.w * 255));
    SDL_RenderDebugText(r, w->pos.x, w->pos.y, label->text);
}

static void dlabel_init(DLabel* label, const char* text, Mel_Vec4 color)
{
    mel_widget_init(&label->base);
    label->base.draw = dlabel_draw;
    label->text = text;
    label->color = color;
}

static void dbutton_draw(Mel_Widget* w, void* ctx)
{
    DButton* btn = (DButton*)w;
    SDL_Renderer* r = (SDL_Renderer*)ctx;

    Mel_Vec4 color;
    if (!w->enabled)
        color = btn->disabled_color;
    else if (w->state & MEL_WIDGET_STATE_PRESSED)
        color = btn->pressed_color;
    else if (w->state & MEL_WIDGET_STATE_HOVERED)
        color = btn->hover_color;
    else
        color = btn->normal_color;

    SDL_FRect rect = { w->pos.x, w->pos.y, w->size.x, w->size.y };
    SDL_SetRenderDrawColor(r,
        (u8)(color.x * 255),
        (u8)(color.y * 255),
        (u8)(color.z * 255),
        (u8)(color.w * 255));
    SDL_RenderFillRect(r, &rect);

    if (btn->text && btn->text[0]) {
        f32 text_w = (f32)strlen(btn->text) * 8.0f;
        f32 text_h = 8.0f;
        f32 tx = w->pos.x + (w->size.x - text_w) * 0.5f;
        f32 ty = w->pos.y + (w->size.y - text_h) * 0.5f;

        Mel_Vec4 tc = btn->text_color;
        if (!w->enabled)
            tc = mel_vec4(tc.x * 0.5f, tc.y * 0.5f, tc.z * 0.5f, tc.w);

        SDL_SetRenderDrawColor(r,
            (u8)(tc.x * 255),
            (u8)(tc.y * 255),
            (u8)(tc.z * 255),
            (u8)(tc.w * 255));
        SDL_RenderDebugText(r, tx, ty, btn->text);
    }
}

static bool dbutton_mouse_down(Mel_Widget* w, Mel_Vec2 pos, i32 button)
{
    MEL_UNUSED(pos);
    if (button != 1) return false;
    w->state |= MEL_WIDGET_STATE_PRESSED;
    return true;
}

static bool dbutton_mouse_up(Mel_Widget* w, Mel_Vec2 pos, i32 button)
{
    if (button != 1) return false;
    if (!(w->state & MEL_WIDGET_STATE_PRESSED)) return false;

    DButton* btn = (DButton*)w;
    if (mel_widget_contains(w, pos)) {
        if (btn->on_click)
            btn->on_click(btn->click_data);
    }
    w->state &= ~MEL_WIDGET_STATE_PRESSED;
    return true;
}

static bool dbutton_mouse_move(Mel_Widget* w, Mel_Vec2 pos)
{
    if (mel_widget_contains(w, pos))
        w->state |= MEL_WIDGET_STATE_HOVERED;
    else
        w->state &= ~MEL_WIDGET_STATE_HOVERED;
    return false;
}

static void dbutton_init(DButton* btn, const char* text, Mel_Vec4 normal)
{
    mel_widget_init(&btn->base);
    btn->base.draw = dbutton_draw;
    btn->base.on_mouse_down = dbutton_mouse_down;
    btn->base.on_mouse_up = dbutton_mouse_up;
    btn->base.on_mouse_move = dbutton_mouse_move;
    btn->text = text;
    btn->normal_color = normal;
    btn->hover_color = mel_vec4(
        normal.x + 0.12f > 1.0f ? 1.0f : normal.x + 0.12f,
        normal.y + 0.12f > 1.0f ? 1.0f : normal.y + 0.12f,
        normal.z + 0.12f > 1.0f ? 1.0f : normal.z + 0.12f,
        normal.w);
    btn->pressed_color = mel_vec4(
        normal.x * 0.6f,
        normal.y * 0.6f,
        normal.z * 0.6f,
        normal.w);
    btn->disabled_color = mel_vec4(0.2f, 0.2f, 0.2f, 1.0f);
    btn->text_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    btn->on_click = nullptr;
    btn->click_data = nullptr;
}

static DPanel   s_root;
static DPanel   s_header;
static DLabel   s_title;
static DPanel   s_button_bar;
static DButton  s_btn_click;
static DButton  s_btn_toggle;
static DButton  s_btn_reset;
static DButton  s_btn_disable;
static DPanel   s_toggle_panel;
static DLabel   s_toggle_label;
static DLabel   s_counter_label;
static DPanel   s_inner_panel;
static DLabel   s_inner_label;
static DPanel   s_status_bar;
static DLabel   s_status_label;
static DPanel   s_highlight;
static DLabel   s_help_label;

static i32 s_click_count;
static bool s_toggle_visible = true;
static bool s_buttons_disabled = false;
static char s_counter_buf[64];
static char s_status_buf[128];
static Mel_Vec2 s_mouse_pos;
static Mel_Widget* s_hovered_widget;

static const char* widget_name(Mel_Widget* w)
{
    if (!w) return "none";
    if (w == &s_root.base) return "root";
    if (w == &s_header.base) return "header";
    if (w == &s_title.base) return "title";
    if (w == &s_button_bar.base) return "button_bar";
    if (w == &s_btn_click.base) return "Click Me!";
    if (w == &s_btn_toggle.base) return "Toggle";
    if (w == &s_btn_reset.base) return "Reset";
    if (w == &s_btn_disable.base) return "Disable";
    if (w == &s_toggle_panel.base) return "toggle_panel";
    if (w == &s_toggle_label.base) return "toggle_label";
    if (w == &s_counter_label.base) return "counter_label";
    if (w == &s_inner_panel.base) return "inner_panel";
    if (w == &s_inner_label.base) return "inner_label";
    if (w == &s_status_bar.base) return "status_bar";
    if (w == &s_status_label.base) return "status_label";
    if (w == &s_help_label.base) return "help_label";
    return "???";
}

static void update_counter_text(void)
{
    snprintf(s_counter_buf, sizeof(s_counter_buf), "Clicks: %d", s_click_count);
    s_counter_label.text = s_counter_buf;
}

static void update_status_text(void)
{
    const char* state = "";
    if (s_hovered_widget) {
        u32 st = s_hovered_widget->state;
        if (st & MEL_WIDGET_STATE_PRESSED) state = " [PRESSED]";
        else if (st & MEL_WIDGET_STATE_HOVERED) state = " [HOVERED]";
        if (st & MEL_WIDGET_STATE_DISABLED) state = " [DISABLED]";
    }
    snprintf(s_status_buf, sizeof(s_status_buf),
        "Mouse: (%.0f, %.0f)  Hit: %s%s",
        s_mouse_pos.x, s_mouse_pos.y, widget_name(s_hovered_widget), state);
    s_status_label.text = s_status_buf;
}

static void on_click(void* user)
{
    MEL_UNUSED(user);
    s_click_count++;
    update_counter_text();
}

static void on_toggle(void* user)
{
    MEL_UNUSED(user);
    s_toggle_visible = !s_toggle_visible;
    mel_widget_set_visible(&s_toggle_panel.base, s_toggle_visible);
}

static void on_reset(void* user)
{
    MEL_UNUSED(user);
    s_click_count = 0;
    update_counter_text();
}

static void on_disable(void* user)
{
    MEL_UNUSED(user);
    s_buttons_disabled = !s_buttons_disabled;
    mel_widget_set_enabled(&s_btn_click.base, !s_buttons_disabled);
    mel_widget_set_enabled(&s_btn_toggle.base, !s_buttons_disabled);
    mel_widget_set_enabled(&s_btn_reset.base, !s_buttons_disabled);
    s_btn_disable.text = s_buttons_disabled ? "Enable" : "Disable";
}

static void build_ui(void)
{
    f32 W = INT_W;
    f32 H = INT_H;
    f32 M = 8;
    f32 BH = 14;

    dpanel_init(&s_root, mel_vec4(0.08f, 0.08f, 0.10f, 1.0f));
    mel_widget_set_position(&s_root.base, mel_vec2(0, 0));
    mel_widget_set_size(&s_root.base, mel_vec2(W, H));

    dpanel_init(&s_header, mel_vec4(0.35f, 0.15f, 0.55f, 1.0f));
    mel_widget_set_position(&s_header.base, mel_vec2(M, M));
    mel_widget_set_size(&s_header.base, mel_vec2(W - M * 2, 16));
    mel_widget_add_child(&s_root.base, &s_header.base);

    dlabel_init(&s_title, "Melody Widget System Demo",
        mel_vec4(1.0f, 1.0f, 1.0f, 1.0f));
    mel_widget_set_position(&s_title.base,
        mel_vec2(M + (W - M * 2 - 25 * 8) * 0.5f, M + 4));
    mel_widget_add_child(&s_header.base, &s_title.base);

    f32 bar_y = M + 16 + 4;
    dpanel_init(&s_button_bar, mel_vec4(0.12f, 0.12f, 0.15f, 1.0f));
    mel_widget_set_position(&s_button_bar.base, mel_vec2(M, bar_y));
    mel_widget_set_size(&s_button_bar.base, mel_vec2(W - M * 2, BH + 4));
    mel_widget_add_child(&s_root.base, &s_button_bar.base);

    f32 btn_w = 80;
    f32 btn_gap = 6;
    f32 btn_x = M + 4;
    f32 btn_y = bar_y + 2;

    dbutton_init(&s_btn_click, "Click Me!", mel_vec4(0.2f, 0.5f, 0.8f, 1.0f));
    mel_widget_set_position(&s_btn_click.base, mel_vec2(btn_x, btn_y));
    mel_widget_set_size(&s_btn_click.base, mel_vec2(btn_w, BH));
    s_btn_click.on_click = on_click;
    mel_widget_add_child(&s_button_bar.base, &s_btn_click.base);

    btn_x += btn_w + btn_gap;
    dbutton_init(&s_btn_toggle, "Toggle", mel_vec4(0.6f, 0.3f, 0.7f, 1.0f));
    mel_widget_set_position(&s_btn_toggle.base, mel_vec2(btn_x, btn_y));
    mel_widget_set_size(&s_btn_toggle.base, mel_vec2(btn_w, BH));
    s_btn_toggle.on_click = on_toggle;
    mel_widget_add_child(&s_button_bar.base, &s_btn_toggle.base);

    btn_x += btn_w + btn_gap;
    dbutton_init(&s_btn_reset, "Reset", mel_vec4(0.7f, 0.35f, 0.2f, 1.0f));
    mel_widget_set_position(&s_btn_reset.base, mel_vec2(btn_x, btn_y));
    mel_widget_set_size(&s_btn_reset.base, mel_vec2(btn_w, BH));
    s_btn_reset.on_click = on_reset;
    mel_widget_add_child(&s_button_bar.base, &s_btn_reset.base);

    btn_x += btn_w + btn_gap;
    dbutton_init(&s_btn_disable, "Disable", mel_vec4(0.5f, 0.5f, 0.5f, 1.0f));
    mel_widget_set_position(&s_btn_disable.base, mel_vec2(btn_x, btn_y));
    mel_widget_set_size(&s_btn_disable.base, mel_vec2(btn_w, BH));
    s_btn_disable.on_click = on_disable;
    mel_widget_add_child(&s_button_bar.base, &s_btn_disable.base);

    f32 panel_y = bar_y + BH + 4 + 4;
    f32 panel_h = H - panel_y - M - 18;
    dpanel_init(&s_toggle_panel, mel_vec4(0.12f, 0.08f, 0.20f, 1.0f));
    mel_widget_set_position(&s_toggle_panel.base, mel_vec2(M, panel_y));
    mel_widget_set_size(&s_toggle_panel.base, mel_vec2(W - M * 2, panel_h));
    mel_widget_add_child(&s_root.base, &s_toggle_panel.base);

    dlabel_init(&s_toggle_label, "This panel toggles with the purple button~",
        mel_vec4(0.8f, 0.7f, 1.0f, 1.0f));
    mel_widget_set_position(&s_toggle_label.base, mel_vec2(M + 6, panel_y + 6));
    mel_widget_add_child(&s_toggle_panel.base, &s_toggle_label.base);

    snprintf(s_counter_buf, sizeof(s_counter_buf), "Clicks: 0");
    dlabel_init(&s_counter_label, s_counter_buf,
        mel_vec4(0.5f, 1.0f, 0.5f, 1.0f));
    mel_widget_set_position(&s_counter_label.base, mel_vec2(M + 6, panel_y + 18));
    mel_widget_add_child(&s_toggle_panel.base, &s_counter_label.base);

    dpanel_init(&s_inner_panel, mel_vec4(0.18f, 0.12f, 0.28f, 1.0f));
    mel_widget_set_position(&s_inner_panel.base, mel_vec2(M + 6, panel_y + 32));
    mel_widget_set_size(&s_inner_panel.base, mel_vec2(W - M * 2 - 12, panel_h - 40));
    mel_widget_add_child(&s_toggle_panel.base, &s_inner_panel.base);

    dlabel_init(&s_inner_label,
        "Nested panel (3 levels deep). Hit test finds me!",
        mel_vec4(0.7f, 0.6f, 0.9f, 1.0f));
    mel_widget_set_position(&s_inner_label.base, mel_vec2(M + 12, panel_y + 38));
    mel_widget_add_child(&s_inner_panel.base, &s_inner_label.base);

    dpanel_init(&s_highlight, mel_vec4(1.0f, 1.0f, 0.0f, 1.0f));
    s_highlight.outline_only = true;
    mel_widget_set_visible(&s_highlight.base, false);
    mel_widget_add_child(&s_root.base, &s_highlight.base);

    f32 status_y = H - M - 10;
    dpanel_init(&s_status_bar, mel_vec4(0.05f, 0.05f, 0.07f, 1.0f));
    mel_widget_set_position(&s_status_bar.base, mel_vec2(M, status_y));
    mel_widget_set_size(&s_status_bar.base, mel_vec2(W - M * 2, 10));
    mel_widget_add_child(&s_root.base, &s_status_bar.base);

    snprintf(s_status_buf, sizeof(s_status_buf), "Mouse: (0, 0)  Hit: none");
    dlabel_init(&s_status_label, s_status_buf,
        mel_vec4(0.6f, 0.6f, 0.6f, 1.0f));
    mel_widget_set_position(&s_status_label.base, mel_vec2(M + 2, status_y + 1));
    mel_widget_add_child(&s_status_bar.base, &s_status_label.base);

    dlabel_init(&s_help_label,
        "Hover to highlight | Click buttons | Toggle/Disable",
        mel_vec4(0.4f, 0.4f, 0.4f, 1.0f));
    mel_widget_set_position(&s_help_label.base, mel_vec2(M, H - 6));
    mel_widget_add_child(&s_root.base, &s_help_label.base);
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    MEL_UNUSED(argc);
    MEL_UNUSED(argv);
    MEL_UNUSED(appstate);

    if (!SDL_Init(SDL_INIT_VIDEO))
        return SDL_APP_FAILURE;

    s_window = SDL_CreateWindow("Melody Widget Demo", WIN_W, WIN_H, 0);
    if (!s_window) return SDL_APP_FAILURE;

    s_renderer = SDL_CreateRenderer(s_window, nullptr);
    if (!s_renderer) return SDL_APP_FAILURE;

    SDL_SetRenderScale(s_renderer, SCALE, SCALE);

    build_ui();

    SDL_Log("Widget demo ready!");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    MEL_UNUSED(appstate);

    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        s_mouse_pos = mel_vec2(event->motion.x / SCALE, event->motion.y / SCALE);
        mel_widget_mouse_move(&s_root.base, s_mouse_pos);

        s_hovered_widget = mel_widget_hit_test(&s_root.base, s_mouse_pos);
        update_status_text();

        if (s_hovered_widget && s_hovered_widget != &s_root.base
            && s_hovered_widget != &s_highlight.base) {
            mel_widget_set_visible(&s_highlight.base, true);
            mel_widget_set_position(&s_highlight.base, mel_vec2(
                s_hovered_widget->pos.x - 1, s_hovered_widget->pos.y - 1));
            mel_widget_set_size(&s_highlight.base, mel_vec2(
                s_hovered_widget->size.x + 2, s_hovered_widget->size.y + 2));
        } else {
            mel_widget_set_visible(&s_highlight.base, false);
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        Mel_Vec2 pos = mel_vec2(event->button.x / SCALE, event->button.y / SCALE);
        mel_widget_mouse_down(&s_root.base, pos, event->button.button);
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        Mel_Vec2 pos = mel_vec2(event->button.x / SCALE, event->button.y / SCALE);
        mel_widget_mouse_up(&s_root.base, pos, event->button.button);
        update_status_text();
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    MEL_UNUSED(appstate);

    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);

    mel_widget_draw(&s_root.base, s_renderer);

    SDL_RenderPresent(s_renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    MEL_UNUSED(appstate);
    MEL_UNUSED(result);

    mel_widget_destroy(&s_root.base);

    if (s_renderer) SDL_DestroyRenderer(s_renderer);
    if (s_window) SDL_DestroyWindow(s_window);
    SDL_Quit();
}
