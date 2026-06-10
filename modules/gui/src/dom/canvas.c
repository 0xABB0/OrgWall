#include "web.h"

#include <paint/paint.h>

EM_JS(void, mel_web__canvas_size, (int id, int w, int h), {
    const el = MelWeb.els[id];
    if (el)
    {
        el.width = w;
        el.height = h;
    }
});

void mel_web__canvas_repaint(Mel_Gui_Node* n)
{
    if (!n)
        return;
    int          id = mel_web__id_of(n);
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (!c || !c->canvas.on_paint)
        return;
    if (n->width <= 0 || n->height <= 0)
        return;

    mel_web__canvas_size(id, n->width, n->height);
    Mel_Drawable d = mel_drawable_borrow((void*)(intptr_t)id, n->width, n->height);
    Mel_Painter  p = mel_painter_begin(d);
    c->canvas.on_paint(n->self, &p, n->width, n->height, mel_gui_user(n->self));
    mel_painter_end(&p);
    mel_drawable_release(d);
}

Mel_Gui_Handle mel_canvas_create_opt(Mel_Gui_Handle parent, Mel_Canvas_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    int id = mel_web__el_create("canvas");
    mel_web__el_class(id, "mel-canvas");
    mel_web__el_append(mel_web__parent_id(n), id);
    n->native = (void*)(intptr_t)id;

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c)
    {
        c->canvas = o.on_;
        c->pointer = o.pointer;
        c->focus = o.focus;
        c->keyboard = o.keyboard;
    }

    if (o.pointer.on_pointer_down || o.pointer.on_pointer_move || o.pointer.on_pointer_up)
        mel_web__on_pointer(id);
    if (o.keyboard.on_key_down || o.keyboard.on_key_up)
        mel_web__on_key(id);
    if (o.focus.on_focus_in || o.focus.on_focus_out)
        mel_web__on_focus(id);
    if (n->hidden)
        mel_web__el_visible(id, 0);
    mel_web__member_sync(h);
    if (mel_canvas_style_any(&o.style))
        mel_canvas_set_style_opt(h, o.style);
    return h;
}
