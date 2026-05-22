#include "gui_internal.h"

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt o)
{
    Mel_Gui_Handle h = mel_gui__create(MEL_GUI_HANDLE_NONE,
        o.x, o.y, o.w, o.h, 0, o.user, false, false, 0,
        &o.lifecycle, NULL, NULL, NULL);

    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return h;

    mel_gui__backend_frame_create(w, o.title);
    return h;
}
