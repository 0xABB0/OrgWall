#include "gui_internal.h"

Mel_Gui_Handle mel_button_create_opt(Mel_Gui_Handle parent, Mel_Button_Opt o)
{
    Mel_Gui_Handle h = mel_gui__create(parent,
        o.x, o.y, o.w, o.h, o.id, o.user, o.disabled, o.hidden, 0,
        &o.lifecycle, &o.focus, &o.pointer, &o.keyboard);

    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return h;

    mel_gui__backend_button_create(w, o.text);
    return h;
}
