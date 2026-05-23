#include "gui_internal.h"

Mel_Gui_Callbacks* mel_gui__ensure_cb(Mel_Gui_Widget* w)
{
    if (!w) return NULL;
    if (!w->cb) {
        w->cb = (Mel_Gui_Callbacks*)mel_calloc(mel_gui__alloc(), sizeof *w->cb);
    }
    return w->cb;
}

void mel_gui_set_lifecycle_cb(Mel_Gui_Handle h, const Mel_Gui_Lifecycle_Cb* cb)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w || !cb) return;
    Mel_Gui_Callbacks* c = mel_gui__ensure_cb(w);
    if (c) c->lifecycle = *cb;
}

void mel_gui_set_focus_cb(Mel_Gui_Handle h, const Mel_Gui_Focus_Cb* cb)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w || !cb) return;
    Mel_Gui_Callbacks* c = mel_gui__ensure_cb(w);
    if (c) c->focus = *cb;
}

void mel_gui_set_pointer_cb(Mel_Gui_Handle h, const Mel_Gui_Pointer_Cb* cb)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w || !cb) return;
    Mel_Gui_Callbacks* c = mel_gui__ensure_cb(w);
    if (c) c->pointer = *cb;
}

void mel_gui_set_keyboard_cb(Mel_Gui_Handle h, const Mel_Gui_Keyboard_Cb* cb)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w || !cb) return;
    Mel_Gui_Callbacks* c = mel_gui__ensure_cb(w);
    if (c) c->keyboard = *cb;
}

void mel_gui__fire_focus_in(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->focus.on_focus_in) {
        w->cb->focus.on_focus_in(h, w->user);
    }
}

void mel_gui__fire_focus_out(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->focus.on_focus_out) {
        w->cb->focus.on_focus_out(h, w->user);
    }
}

void mel_gui__fire_click(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->pointer.on_click) {
        w->cb->pointer.on_click(h, w->user);
    }
}

void mel_gui__fire_key_down(Mel_Gui_Handle h, Mel_Key key)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->keyboard.on_key_down) {
        w->cb->keyboard.on_key_down(h, key, w->user);
    }
}

void mel_gui__fire_key_up(Mel_Gui_Handle h, Mel_Key key)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->keyboard.on_key_up) {
        w->cb->keyboard.on_key_up(h, key, w->user);
    }
}

void mel_gui__fire_char(Mel_Gui_Handle h, u32 codepoint)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->keyboard.on_char) {
        w->cb->keyboard.on_char(h, codepoint, w->user);
    }
}

void mel_gui__fire_pointer_down(Mel_Gui_Handle h, i32 x, i32 y)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->pointer.on_pointer_down) {
        w->cb->pointer.on_pointer_down(h, x, y, w->user);
    }
}

void mel_gui__fire_pointer_up(Mel_Gui_Handle h, i32 x, i32 y)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->pointer.on_pointer_up) {
        w->cb->pointer.on_pointer_up(h, x, y, w->user);
    }
}

void mel_gui__fire_pointer_move(Mel_Gui_Handle h, i32 x, i32 y)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->pointer.on_pointer_move) {
        w->cb->pointer.on_pointer_move(h, x, y, w->user);
    }
}

void mel_gui__fire_resize(Mel_Gui_Handle h, i32 w, i32 height)
{
    Mel_Gui_Widget* widget = mel_gui__get(h);
    if (!widget) return;
    widget->width  = w;
    widget->height = height;
    if (widget->cb && widget->cb->lifecycle.on_resize) {
        widget->cb->lifecycle.on_resize(h, w, height, widget->user);
    }
    if (widget->layout) mel_gui__layout_arrange(h);
}

void mel_gui__fire_show(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->lifecycle.on_show) {
        w->cb->lifecycle.on_show(h, w->user);
    }
}

void mel_gui__fire_hide(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->lifecycle.on_hide) {
        w->cb->lifecycle.on_hide(h, w->user);
    }
}

void mel_gui__fire_enable(Mel_Gui_Handle h, bool enabled)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w && w->cb && w->cb->lifecycle.on_enable) {
        w->cb->lifecycle.on_enable(h, enabled, w->user);
    }
}

bool mel_gui__fire_close(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w || !w->cb || !w->cb->lifecycle.on_close) return true;
    return w->cb->lifecycle.on_close(h, w->user);
}
