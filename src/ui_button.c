#include "ui_button.h"

static void button_draw(Mel_Widget* w, Mel_SpriteBatch* batch)
{
    Mel_Button* btn = (Mel_Button*)w;

    Mel_Vec4 color = btn->normal_color;
    if (btn->pressed) color = btn->pressed_color;
    else if (btn->hovered) color = btn->hover_color;

    mel_sprite_batch_draw(batch, w->pos.x, w->pos.y, w->size.x, w->size.y, color);
}

static bool button_mouse_down(Mel_Widget* w, Mel_Vec2 pos, i32 button)
{
    MEL_UNUSED(pos);
    if (button != 1) return false;

    Mel_Button* btn = (Mel_Button*)w;
    btn->pressed = true;
    return true;
}

static bool button_mouse_up(Mel_Widget* w, Mel_Vec2 pos, i32 button)
{
    if (button != 1) return false;

    Mel_Button* btn = (Mel_Button*)w;

    if (btn->pressed && mel_widget_contains(w, pos))
    {
        if (btn->on_click)
        {
            btn->on_click(btn->user_data);
        }
    }

    btn->pressed = false;
    return true;
}

static bool button_mouse_move(Mel_Widget* w, Mel_Vec2 pos)
{
    Mel_Button* btn = (Mel_Button*)w;
    btn->hovered = mel_widget_contains(w, pos);
    return false;
}

static const Mel_Widget_VTable s_button_vtable = {
    .draw = button_draw,
    .on_mouse_down = button_mouse_down,
    .on_mouse_up = button_mouse_up,
    .on_mouse_move = button_mouse_move,
};

void mel_button_init(Mel_Button* button)
{
    assert(button != nullptr);
    mel_widget_init(&button->base, &s_button_vtable, MEL_WIDGET_TYPE_BUTTON);

    button->normal_color = mel_vec4(0.3f, 0.3f, 0.3f, 1.0f);
    button->hover_color = mel_vec4(0.4f, 0.4f, 0.4f, 1.0f);
    button->pressed_color = mel_vec4(0.2f, 0.2f, 0.2f, 1.0f);
    button->hovered = false;
    button->pressed = false;
    button->on_click = nullptr;
    button->user_data = nullptr;
}
