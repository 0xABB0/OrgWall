#include "web.h"

// Checkbox is a <label> wrapping a real <input type=checkbox> plus a text node,
// so the click target and accessible label come for free. The slider is an
// <input type=range>.

EM_JS(void, mel_web__checkbox_label, (int id, const char* s), {
    const el = MelWeb.els[id]; if (!el) return;
    el.innerHTML = '';
    const inp = document.createElement('input');
    inp.type = 'checkbox';
    el.appendChild(inp);
    el.appendChild(document.createTextNode(UTF8ToString(s)));
});
EM_JS(void, mel_web__checkbox_set, (int id, int v), {
    const el = MelWeb.els[id]; const i = el && el.querySelector('input');
    if (i) i.checked = !!v;
});
EM_JS(int, mel_web__checkbox_get, (int id), {
    const el = MelWeb.els[id]; const i = el && el.querySelector('input');
    return i && i.checked ? 1 : 0;
});

EM_JS(void, mel_web__slider_setup, (int id, int lo, int hi, int value), {
    const el = MelWeb.els[id]; if (!el) return;
    el.type = 'range'; el.min = lo; el.max = hi; el.value = value;
});
EM_JS(void, mel_web__slider_set, (int id, int value), {
    const el = MelWeb.els[id]; if (el) el.value = value;
});
EM_JS(int, mel_web__slider_value, (int id), {
    const el = MelWeb.els[id]; return el ? (parseInt(el.value) | 0) : 0;
});

Mel_Gui_Handle mel_label_create_opt(Mel_Gui_Handle parent, Mel_Label_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int id = mel_web__el_create("div");
    mel_web__el_class(id, "mel-label");
    mel_web__el_append(mel_web__parent_id(n), id);
    n->native = (void*)(intptr_t)id;

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c) c->kind = MEL_WEB_TEXT;

    char b[1024];
    mel_web__el_text(id, mel_web__cstr(o.text, b, sizeof b));
    if (n->hidden) mel_web__el_visible(id, 0);
    return h;
}

Mel_Gui_Handle mel_button_create_opt(Mel_Gui_Handle parent, Mel_Button_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int id = mel_web__el_create("button");
    mel_web__el_class(id, "mel-button");
    mel_web__el_append(mel_web__parent_id(n), id);
    n->native = (void*)(intptr_t)id;

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c) {
        c->kind     = MEL_WEB_TEXT;
        c->pointer  = o.pointer;
        c->focus    = o.focus;
        c->keyboard = o.keyboard;
    }

    char b[1024];
    mel_web__el_text(id, mel_web__cstr(o.text, b, sizeof b));
    mel_web__on_click(id);
    if (o.focus.on_focus_in || o.focus.on_focus_out)    mel_web__on_focus(id);
    if (o.keyboard.on_key_down || o.keyboard.on_key_up) mel_web__on_key(id);
    if (o.disabled) mel_web__el_enabled(id, 0);
    if (n->hidden)  mel_web__el_visible(id, 0);
    return h;
}

Mel_Gui_Handle mel_textfield_create_opt(Mel_Gui_Handle parent, Mel_TextField_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int id = mel_web__el_create("input");
    mel_web__el_class(id, "mel-textfield");
    mel_web__el_append(mel_web__parent_id(n), id);
    n->native = (void*)(intptr_t)id;

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c) {
        c->kind      = MEL_WEB_INPUT;
        c->textfield = o.on_;
        c->focus     = o.focus;
        c->keyboard  = o.keyboard;
    }

    char b[1024];
    mel_web__el_set_value(id, mel_web__cstr(o.text, b, sizeof b));
    if (o.on_.on_text_changed)                          mel_web__on_input(id);
    if (o.focus.on_focus_in || o.focus.on_focus_out)    mel_web__on_focus(id);
    if (o.keyboard.on_key_down || o.keyboard.on_key_up) mel_web__on_key(id);
    if (o.disabled) mel_web__el_enabled(id, 0);
    if (n->hidden)  mel_web__el_visible(id, 0);
    return h;
}

Mel_Gui_Handle mel_checkbox_create_opt(Mel_Gui_Handle parent, Mel_CheckBox_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int id = mel_web__el_create("label");
    mel_web__el_class(id, "mel-checkbox");
    mel_web__el_append(mel_web__parent_id(n), id);
    n->native = (void*)(intptr_t)id;

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c) {
        c->checkbox = o.on_;
        c->focus    = o.focus;
    }

    char b[512];
    mel_web__checkbox_label(id, mel_web__cstr(o.text, b, sizeof b));
    mel_web__checkbox_set(id, o.checked);
    mel_web__on_check(id);
    if (n->hidden) mel_web__el_visible(id, 0);
    return h;
}

bool mel_checkbox_checked(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    return n ? mel_web__checkbox_get(mel_web__id_of(n)) != 0 : false;
}

Mel_Gui_Handle mel_slider_create_opt(Mel_Gui_Handle parent, Mel_Slider_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int id = mel_web__el_create("input");
    mel_web__el_class(id, "mel-slider");
    mel_web__el_append(mel_web__parent_id(n), id);
    n->native = (void*)(intptr_t)id;

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c) {
        c->slider = o.on_;
        c->focus  = o.focus;
    }

    mel_web__slider_setup(id, o.min_value, o.max_value, o.value);
    if (o.on_.on_value_changed)                      mel_web__on_slider(id);
    if (o.focus.on_focus_in || o.focus.on_focus_out) mel_web__on_focus(id);
    if (o.disabled) mel_web__el_enabled(id, 0);
    if (n->hidden)  mel_web__el_visible(id, 0);
    return h;
}

i32 mel_slider_value(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    return n ? mel_web__slider_value(mel_web__id_of(n)) : 0;
}

void mel_slider_set_value(Mel_Gui_Handle h, i32 value)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (n) mel_web__slider_set(mel_web__id_of(n), value);
}
