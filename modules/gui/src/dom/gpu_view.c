#include <stdio.h>

#include "web.h"

Mel_Gui_Handle mel_gpu_view_create_opt(Mel_Gui_Handle parent, Mel_Gpu_View_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int id = mel_web__el_create("canvas");
    mel_web__el_class(id, "mel-gpu");

    char html_id[64];
    snprintf(html_id, sizeof html_id, "mel-gpu-%d", id);
    mel_web__el_html_id(id, html_id);

    mel_web__el_append(mel_web__parent_id(n), id);
    n->native = (void*)(intptr_t)id;

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c) {
        c->gpu_view = o.on_;
        c->pointer  = o.pointer;
        c->focus    = o.focus;
        c->keyboard = o.keyboard;
    }

    if (o.pointer.on_pointer_down || o.pointer.on_pointer_move || o.pointer.on_pointer_up)
        mel_web__on_pointer(id);
    if (o.keyboard.on_key_down || o.keyboard.on_key_up) mel_web__on_key(id);
    if (o.focus.on_focus_in || o.focus.on_focus_out)    mel_web__on_focus(id);
    if (n->hidden) mel_web__el_visible(id, 0);
    return h;
}

void* mel_gpu_view_surface(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return NULL;
    int id = mel_web__id_of(n);
    if (!id) return NULL;
    static char selector[64];
    snprintf(selector, sizeof selector, "#mel-gpu-%d", id);
    return selector;
}
