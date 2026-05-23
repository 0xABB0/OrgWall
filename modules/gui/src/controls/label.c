#include "../gui_internal.h"

Mel_Gui_Handle mel_label_create_opt(Mel_Gui_Handle parent, Mel_Label_Opt o)
{
    Mel_Gui_Handle h = mel_gui__create(parent,
        o.x, o.y, o.w, o.h, o.id, o.user, false, o.hidden, 0,
        &o.lifecycle, NULL, NULL, NULL,
        &o.layoutable, NULL);

    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return h;

    mel_gui__backend_label_create(w, o.text);
    return h;
}
