#include "web.h"

EM_JS(void, mel_web__groupbox_legend, (int id, const char* s), {
    const el = MelWeb.els[id]; if (!el) return;
    const lg = document.createElement('legend');
    lg.textContent = UTF8ToString(s);
    el.appendChild(lg);
});

Mel_Gui_Handle mel_panel_create_opt(Mel_Gui_Handle parent, Mel_Panel_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int id = mel_web__el_create("div");
    mel_web__el_class(id, "mel-panel");
    mel_web__el_append(mel_web__parent_id(n), id);
    n->native = (void*)(intptr_t)id; // children parent directly into the panel

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c) {
        c->pointer  = o.pointer;
        c->focus    = o.focus;
        c->keyboard = o.keyboard;
    }
    if (o.pointer.on_pointer_down || o.pointer.on_pointer_move || o.pointer.on_pointer_up
        || o.pointer.on_click)
        mel_web__on_pointer(id);
    if (o.disabled) mel_web__el_enabled(id, 0);
    if (n->hidden)  mel_web__el_visible(id, 0);
    return h;
}

Mel_Gui_Handle mel_groupbox_create_opt(Mel_Gui_Handle parent, Mel_GroupBox_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int box = mel_web__el_create("fieldset");
    mel_web__el_class(box, "mel-groupbox");
    mel_web__el_append(mel_web__parent_id(n), box);

    char b[512];
    mel_web__groupbox_legend(box, mel_web__cstr(o.title, b, sizeof b));

    int body = mel_web__el_create("div");
    mel_web__el_class(body, "mel-groupbox-body");
    mel_web__el_append(box, body);

    n->native  = (void*)(intptr_t)box;
    n->content = (void*)(intptr_t)body; // children parent into the body box

    Mel_Web_Ctl* c = mel_web__ctl_new(box, h);
    if (c) c->focus = o.focus;
    if (n->hidden) mel_web__el_visible(box, 0);
    return h;
}

Mel_Gui_Handle mel_scrollview_create_opt(Mel_Gui_Handle parent, Mel_ScrollView_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int outer = mel_web__el_create("div");
    mel_web__el_class(outer, "mel-scroll");
    mel_web__el_append(mel_web__parent_id(n), outer);

    int inner = mel_web__el_create("div");
    mel_web__el_append(outer, inner);
    i32 cw = o.content_w > 0 ? o.content_w : 0;
    i32 ch = o.content_h > 0 ? o.content_h : 0;
    mel_web__el_bounds(inner, 0, 0, cw, ch);

    n->native  = (void*)(intptr_t)outer;
    n->content = (void*)(intptr_t)inner; // scrollable content host

    Mel_Web_Ctl* c = mel_web__ctl_new(outer, h);
    if (c) c->focus = o.focus;
    if (o.disabled) mel_web__el_enabled(outer, 0);
    if (n->hidden)  mel_web__el_visible(outer, 0);
    return h;
}
