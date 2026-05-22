#include "gui_internal.h"

Mel_Gui_Handle mel_canvas_create_opt(Mel_Gui_Handle parent, Mel_Canvas_Opt o)
{
    Mel_Gui_Handle h = mel_gui__create(parent,
        o.x, o.y, o.w, o.h, o.id, o.user, false, o.hidden,
        sizeof(Mel_Gui_Canvas_Impl),
        &o.lifecycle, &o.focus, &o.pointer, &o.keyboard);

    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return h;

    Mel_Gui_Canvas_Impl* impl = (Mel_Gui_Canvas_Impl*)w->impl;
    if (impl) impl->on_ = o.on_;

    mel_gui__backend_canvas_create(w, STR8_EMPTY);
    return h;
}
