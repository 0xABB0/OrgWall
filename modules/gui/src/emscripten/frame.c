#include "web.h"

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         o.initial_state == MEL_FRAME_HIDDEN, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int id = mel_web__el_create("div");
    mel_web__el_class(id, "mel-frame");
    mel_web__el_append(0, id);
    n->native = (void*)(intptr_t)id;

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c) {
        c->kind     = MEL_WEB_FRAME;
        c->focus    = o.focus;
        c->keyboard = o.keyboard;
    }

    if (o.title.len) { char b[512]; mel_web__el_title(mel_web__cstr(o.title, b, sizeof b)); }
    mel_web__el_visible(id, o.initial_state != MEL_FRAME_HIDDEN);
    if (o.keyboard.on_key_down || o.keyboard.on_key_up) mel_web__on_key(id);
    if (o.focus.on_focus_in || o.focus.on_focus_out)    mel_web__on_focus(id);

    mel_gui__frames_inc();
    return h;
}

Mel_Frame_Insets mel_frame_insets(Mel_Gui_Handle h)
{
    (void)h;
    return (Mel_Frame_Insets){0};
}
