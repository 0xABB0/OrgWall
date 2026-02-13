#include "ui.widget.button.h"
#include "sprite.batch.h"

static void wbutton_draw(Mel_Widget* w, void* ctx)
{
    Mel_WButton* btn = (Mel_WButton*)w;
    Mel_SpriteBatch* batch = (Mel_SpriteBatch*)ctx;

    Mel_Vec4 color = btn->normal_color;
    if (w->state & MEL_WIDGET_STATE_PRESSED)
        color = btn->pressed_color;
    else if (w->state & MEL_WIDGET_STATE_HOVERED)
        color = btn->hover_color;

    mel_sprite_batch_draw(batch, w->pos.x, w->pos.y, w->size.x, w->size.y, color);
}

static bool wbutton_mouse_down(Mel_Widget* w, Mel_Vec2 pos, i32 button)
{
    MEL_UNUSED(pos);
    if (button != 1) return false;

    w->state |= MEL_WIDGET_STATE_PRESSED;
    return true;
}

static bool wbutton_mouse_up(Mel_Widget* w, Mel_Vec2 pos, i32 button)
{
    if (button != 1) return false;
    if (!(w->state & MEL_WIDGET_STATE_PRESSED)) return false;

    Mel_WButton* btn = (Mel_WButton*)w;

    if (mel_widget_contains(w, pos)) {
        if (btn->on_click)
            btn->on_click(btn->click_data);
    }

    w->state &= ~MEL_WIDGET_STATE_PRESSED;
    return true;
}

static bool wbutton_mouse_move(Mel_Widget* w, Mel_Vec2 pos)
{
    if (mel_widget_contains(w, pos))
        w->state |= MEL_WIDGET_STATE_HOVERED;
    else
        w->state &= ~MEL_WIDGET_STATE_HOVERED;
    return false;
}

void mel_wbutton_init(Mel_WButton* button)
{
    assert(button != nullptr);
    mel_widget_init(&button->base);
    button->base.draw = wbutton_draw;
    button->base.on_mouse_down = wbutton_mouse_down;
    button->base.on_mouse_up = wbutton_mouse_up;
    button->base.on_mouse_move = wbutton_mouse_move;
    button->normal_color = mel_vec4(0.3f, 0.3f, 0.3f, 1.0f);
    button->hover_color = mel_vec4(0.4f, 0.4f, 0.4f, 1.0f);
    button->pressed_color = mel_vec4(0.2f, 0.2f, 0.2f, 1.0f);
    button->on_click = nullptr;
    button->click_data = nullptr;
}
