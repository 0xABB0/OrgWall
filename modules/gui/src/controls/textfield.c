#include "../gui_internal.h"

Mel_Gui_Handle mel_textfield_create_opt(Mel_Gui_Handle parent, Mel_TextField_Opt o)
{
    Mel_Gui_Handle h = mel_gui__create(parent,
        o.x, o.y, o.w, o.h, o.id, o.user, o.disabled, o.hidden,
        sizeof(Mel_Gui_TextField_Impl),
        &o.lifecycle, &o.focus, NULL, &o.keyboard,
        &o.layoutable, NULL);

    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return h;

    Mel_Gui_TextField_Impl* impl = (Mel_Gui_TextField_Impl*)w->impl;
    if (impl) impl->on_ = o.on_;

    mel_gui__backend_textfield_create(w, o.text);
    return h;
}
