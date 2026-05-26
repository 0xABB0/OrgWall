#include "web.h"

#include <gui/controls/tabview.h>
#include <gui/controls/splitter.h>
#include <gui/controls/dialog.h>

// Tabs: a bar of buttons over a stack of absolutely-filled page divs; only the
// selected page is shown. Splitter: a flex row/column of relatively-positioned
// panes. Dialog: a centered frame-like overlay.

EM_JS(void, mel_web__tab_bar_setup, (int tv, int bar, int pages), {
    const t = MelWeb.els[tv], b = MelWeb.els[bar], p = MelWeb.els[pages];
    if (!t || !b || !p) return;
    b.style.position = 'absolute'; b.style.left = '0'; b.style.top = '0';
    b.style.right = '0'; b.style.height = '28px'; b.style.display = 'flex';
    p.style.position = 'absolute'; p.style.left = '0'; p.style.top = '28px';
    p.style.right = '0'; p.style.bottom = '0';
});
EM_JS(void, mel_web__tab_add_button, (int tv, int bar, int index, const char* title), {
    const b = MelWeb.els[bar]; if (!b) return;
    const btn = document.createElement('button');
    btn.textContent = UTF8ToString(title);
    btn.addEventListener('click', () => { _mel_web__ev_select(tv, index); });
    b.appendChild(btn);
});
EM_JS(void, mel_web__tab_show, (int pages, int index), {
    const p = MelWeb.els[pages]; if (!p) return;
    for (let i = 0; i < p.children.length; i++)
        p.children[i].style.display = (i === index) ? 'block' : 'none';
});
EM_JS(void, mel_web__flex_item, (int id, int basis, int vertical), {
    const el = MelWeb.els[id]; if (!el) return;
    el.style.position = 'relative';
    el.style.flex = basis > 0 ? ('0 0 ' + basis + 'px') : '1 1 0';
    el.style.minWidth = '0'; el.style.minHeight = '0';
    void vertical;
});
EM_JS(void, mel_web__flex_dir, (int id, int vertical), {
    const el = MelWeb.els[id]; if (el) el.style.flexDirection = vertical ? 'column' : 'row';
});
EM_JS(void, mel_web__center_overlay, (int id), {
    const el = MelWeb.els[id]; if (!el) return;
    el.style.left = '50%'; el.style.top = '50%';
    el.style.transform = 'translate(-50%, -50%)';
    el.style.zIndex = '1000';
    el.style.boxShadow = '0 8px 32px rgba(0,0,0,0.5)';
});

// --- tabview ---

Mel_Gui_Handle mel_tabview_create_opt(Mel_Gui_Handle parent, Mel_TabView_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int tv = mel_web__el_create("div");
    mel_web__el_class(tv, "mel-tabview");
    mel_web__el_append(mel_web__parent_id(n), tv);

    int bar   = mel_web__el_create("div");
    int pages = mel_web__el_create("div");
    mel_web__el_append(tv, bar);
    mel_web__el_append(tv, pages);
    mel_web__tab_bar_setup(tv, bar, pages);

    n->native  = (void*)(intptr_t)tv;
    n->content = (void*)(intptr_t)pages;

    Mel_Web_Ctl* c = mel_web__ctl_new(tv, h);
    if (c) {
        c->on_select = o.on_select;
        c->focus     = o.focus;
        c->aux0      = bar;
        c->aux1      = 0;   // tab count
        c->aux2      = 0;   // selected
    }
    if (n->hidden) mel_web__el_visible(tv, 0);
    return h;
}

Mel_Gui_Handle mel_tab_create_opt(Mel_Gui_Handle tabview, Mel_Tab_Opt o)
{
    Mel_Gui_Node* tn = mel_gui__node(tabview);
    if (!tn) return MEL_GUI_HANDLE_NONE;
    int tv = mel_web__id_of(tn);
    Mel_Web_Ctl* tc = mel_web__ctl(tv);
    if (!tc) return MEL_GUI_HANDLE_NONE;

    Mel_Gui_Handle h = mel_gui__node_new(tabview, 0, 0, 0, 0, o.id, o.user, false,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int page = mel_web__el_create("div");
    mel_web__el_append((int)(intptr_t)tn->content, page);
    mel_web__el_bounds(page, 0, 0, 0, 0); // sized to fill via inset below
    n->native  = (void*)(intptr_t)page;
    n->content = (void*)(intptr_t)page;

    int index = tc->aux1++;
    char b[256];
    mel_web__tab_add_button(tv, tc->aux0, index, mel_web__cstr(o.title, b, sizeof b));
    mel_web__tab_show((int)(intptr_t)tn->content, tc->aux2);
    return h;
}

void mel_tabview_select(Mel_Gui_Handle tabview, i32 index)
{
    Mel_Gui_Node* n = mel_gui__node(tabview);
    if (!n) return;
    Mel_Web_Ctl* c = mel_web__ctl(mel_web__id_of(n));
    if (!c) return;
    c->aux2 = index;
    mel_web__tab_show((int)(intptr_t)n->content, index);
}

i32 mel_tabview_selected(Mel_Gui_Handle tabview)
{
    Mel_Gui_Node* n = mel_gui__node(tabview);
    Mel_Web_Ctl* c = n ? mel_web__ctl(mel_web__id_of(n)) : NULL;
    return c ? c->aux2 : 0;
}

// --- splitter ---

Mel_Gui_Handle mel_splitter_create_opt(Mel_Gui_Handle parent, Mel_Splitter_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int id = mel_web__el_create("div");
    mel_web__el_class(id, "mel-splitter");
    mel_web__el_append(mel_web__parent_id(n), id);
    mel_web__flex_dir(id, o.orientation == MEL_SPLIT_VERTICAL);
    n->native = (void*)(intptr_t)id;

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c) {
        c->focus = o.focus;
        c->aux0  = (o.orientation == MEL_SPLIT_VERTICAL);
    }
    if (n->hidden) mel_web__el_visible(id, 0);
    return h;
}

Mel_Gui_Handle mel_splitpane_create_opt(Mel_Gui_Handle splitter, Mel_SplitPane_Opt o)
{
    Mel_Gui_Node* sn = mel_gui__node(splitter);
    if (!sn) return MEL_GUI_HANDLE_NONE;
    Mel_Web_Ctl* sc = mel_web__ctl(mel_web__id_of(sn));

    Mel_Gui_Handle h = mel_gui__node_new(splitter, 0, 0, 0, 0, o.id, o.user, false,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int pane = mel_web__el_create("div");
    mel_web__el_append(mel_web__id_of(sn), pane);
    mel_web__flex_item(pane, o.initial_size, sc ? sc->aux0 : 0);
    n->native  = (void*)(intptr_t)pane;
    n->content = (void*)(intptr_t)pane;
    mel_web__ctl_new(pane, h);
    return h;
}

// --- dialog ---

Mel_Gui_Handle mel_dialog_create_opt(Mel_Dialog_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         false, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int id = mel_web__el_create("div");
    mel_web__el_class(id, "mel-frame");
    mel_web__el_append(0, id);
    mel_web__center_overlay(id);
    n->native = (void*)(intptr_t)id;

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c) {
        c->kind      = MEL_WEB_FRAME;
        c->on_select = o.on_.on_result; // (handle, result, user)
        c->focus     = o.focus;
        c->keyboard  = o.keyboard;
    }
    if (o.title.len) { char b[512]; mel_web__el_title(mel_web__cstr(o.title, b, sizeof b)); }
    mel_gui__frames_inc();
    return h;
}

void mel_dialog_close(Mel_Gui_Handle dialog, i32 result)
{
    Mel_Gui_Node* n = mel_gui__node(dialog);
    if (!n) return;
    Mel_Web_Ctl* c = mel_web__ctl(mel_web__id_of(n));
    if (c && c->on_select) c->on_select(dialog, result, mel_gui_user(dialog));
    mel_gui_set_visible(dialog, false);
}
