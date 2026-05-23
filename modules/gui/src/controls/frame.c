#include "../gui_internal.h"

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt o)
{
    Mel_Gui_Handle h = mel_gui__create(MEL_GUI_HANDLE_NONE,
        o.x, o.y, o.w, o.h, 0, o.user,
        false, o.initial_state == MEL_FRAME_HIDDEN,
        sizeof(Mel_Gui_Frame_Impl),
        &o.lifecycle, &o.focus, NULL, &o.keyboard,
        NULL, o.layout);

    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return h;

    Mel_Gui_Frame_Impl* fi = (Mel_Gui_Frame_Impl*)w->impl;
    if (fi) {
        fi->min_w         = o.min_w;
        fi->min_h         = o.min_h;
        fi->max_w         = o.max_w;
        fi->max_h         = o.max_h;
        fi->resizable     = !o.not_resizable;
        fi->decorated     = !o.undecorated;
        fi->closable      = !o.not_closable;
        fi->owner         = o.owner;
        fi->icon_large    = o.icon_large;
        fi->icon_small    = o.icon_small;
        fi->initial_state = (u8)o.initial_state;
    }

    mel_gui__backend_frame_create(w, o.title);
    return h;
}
