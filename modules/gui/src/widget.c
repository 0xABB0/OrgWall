#include "gui_internal.h"

void mel_gui_set_text(Mel_Gui_Handle h, str8 text)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w) mel_gui__backend_set_text(w, text);
}

size mel_gui_get_text(Mel_Gui_Handle h, char* buf, size cap)
{
    if (buf && cap > 0) buf[0] = 0;
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w || !buf || cap <= 0) return 0;
    return mel_gui__backend_get_text(w, buf, cap);
}

void mel_gui_set_bounds(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return;
    w->x = x;
    w->y = y;
    w->width = width;
    w->height = height;
    mel_gui__backend_set_bounds(w, x, y, width, height);
}

void mel_gui_set_visible(Mel_Gui_Handle h, bool visible)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return;
    w->hidden = !visible;
    mel_gui__backend_set_visible(w, visible);
}

void mel_gui_set_enabled(Mel_Gui_Handle h, bool enabled)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return;
    w->disabled = !enabled;
    mel_gui__backend_set_enabled(w, enabled);
}

void mel_gui_set_focus(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w) mel_gui__backend_set_focus(w);
}

void mel_gui_invalidate(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w) mel_gui__backend_invalidate(w);
}

u32 mel_gui_id(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    return w ? w->id : 0;
}

void mel_gui_set_user(Mel_Gui_Handle h, void* user)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w) w->user = user;
}

void* mel_gui_user(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    return w ? w->user : NULL;
}

void* mel_gui_native_handle(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    return w ? w->native : NULL;
}
