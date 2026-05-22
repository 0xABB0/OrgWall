#include "../gui_internal.h"

Mel_Gui_Handle mel_slider_create_opt(Mel_Gui_Handle parent, Mel_Slider_Opt o)
{
    Mel_Gui_Handle h = mel_gui__create(parent,
        o.x, o.y, o.w, o.h, o.id, o.user, o.disabled, o.hidden,
        sizeof(Mel_Gui_Slider_Impl),
        &o.lifecycle, &o.focus, NULL, &o.keyboard);

    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return h;

    Mel_Gui_Slider_Impl* impl = (Mel_Gui_Slider_Impl*)w->impl;
    if (impl) {
        impl->on_       = o.on_;
        impl->min_value = o.min_value;
        impl->max_value = (o.max_value > o.min_value) ? o.max_value : o.min_value + 100;
        impl->value     = o.value;
    }

    mel_gui__backend_slider_create(w, STR8_EMPTY);
    return h;
}

i32 mel_slider_value(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    return w ? mel_gui__backend_slider_value(w) : 0;
}

void mel_slider_set_value(Mel_Gui_Handle h, i32 value)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (w) mel_gui__backend_slider_set_value(w, value);
}
