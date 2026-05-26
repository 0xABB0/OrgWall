#include "web.h"

// Each painter op fetches the 2D context fresh and draws straight onto it; the
// canvas backing store is sized to the widget on (re)paint.

EM_JS(void, mel_web__canvas_size, (int id, int w, int h), {
    const el = MelWeb.els[id]; if (el) { el.width = w; el.height = h; }
});
EM_JS(void, mel_web__paint_clear, (int id, unsigned rgba), {
    const el = MelWeb.els[id]; if (!el) return;
    const c = el.getContext('2d');
    c.fillStyle = MelWeb.css(rgba);
    c.fillRect(0, 0, el.width, el.height);
});
EM_JS(void, mel_web__paint_fill_rect, (int id, float x, float y, float w, float h, unsigned rgba), {
    const el = MelWeb.els[id]; if (!el) return;
    const c = el.getContext('2d');
    c.fillStyle = MelWeb.css(rgba);
    c.fillRect(x, y, w, h);
});
EM_JS(void, mel_web__paint_fill_ellipse, (int id, float x, float y, float w, float h, unsigned rgba), {
    const el = MelWeb.els[id]; if (!el) return;
    const c = el.getContext('2d');
    c.fillStyle = MelWeb.css(rgba);
    c.beginPath();
    c.ellipse(x + w / 2, y + h / 2, w / 2, h / 2, 0, 0, Math.PI * 2);
    c.fill();
});
EM_JS(void, mel_web__paint_stroke_rect, (int id, float x, float y, float w, float h, unsigned rgba, float width), {
    const el = MelWeb.els[id]; if (!el) return;
    const c = el.getContext('2d');
    c.strokeStyle = MelWeb.css(rgba);
    c.lineWidth = width;
    c.strokeRect(x, y, w, h);
});
EM_JS(void, mel_web__paint_line, (int id, float ax, float ay, float bx, float by, unsigned rgba, float width), {
    const el = MelWeb.els[id]; if (!el) return;
    const c = el.getContext('2d');
    c.strokeStyle = MelWeb.css(rgba);
    c.lineWidth = width;
    c.beginPath();
    c.moveTo(ax, ay);
    c.lineTo(bx, by);
    c.stroke();
});
EM_JS(void, mel_web__paint_round_rect, (int id, float x, float y, float w, float h, float r, unsigned rgba), {
    const el = MelWeb.els[id]; if (!el) return;
    const c = el.getContext('2d');
    c.fillStyle = MelWeb.css(rgba);
    c.beginPath();
    if (c.roundRect) c.roundRect(x, y, w, h, r); else c.rect(x, y, w, h);
    c.fill();
});
EM_JS(void, mel_web__paint_text, (int id, const char* s, float x, float y, unsigned rgba, float size), {
    const el = MelWeb.els[id]; if (!el) return;
    const c = el.getContext('2d');
    c.fillStyle = MelWeb.css(rgba);
    c.font = size + 'px system-ui, sans-serif';
    c.textBaseline = 'top';
    c.fillText(UTF8ToString(s), x, y);
});

void mel_web__canvas_repaint(Mel_Gui_Node* n)
{
    if (!n) return;
    int id = mel_web__id_of(n);
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (!c || !c->canvas.on_paint) return;
    if (n->width <= 0 || n->height <= 0) return;

    mel_web__canvas_size(id, n->width, n->height);
    Mel_Painter p = { .canvas = id, .w = n->width, .h = n->height };
    c->canvas.on_paint(n->self, &p, n->width, n->height, mel_gui_user(n->self));
}

Mel_Gui_Handle mel_canvas_create_opt(Mel_Gui_Handle parent, Mel_Canvas_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    int id = mel_web__el_create("canvas");
    mel_web__el_class(id, "mel-canvas");
    mel_web__el_append(mel_web__parent_id(n), id);
    n->native = (void*)(intptr_t)id;

    Mel_Web_Ctl* c = mel_web__ctl_new(id, h);
    if (c) {
        c->canvas   = o.on_;
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

void mel_painter_clear(Mel_Painter* p, Mel_Color color)
{
    mel_web__paint_clear(p->canvas, mel_web__rgba(color));
}
void mel_painter_fill_rect(Mel_Painter* p, Mel_Rect r, Mel_Color color)
{
    mel_web__paint_fill_rect(p->canvas, r.x, r.y, r.w, r.h, mel_web__rgba(color));
}
void mel_painter_fill_ellipse(Mel_Painter* p, Mel_Rect r, Mel_Color color)
{
    mel_web__paint_fill_ellipse(p->canvas, r.x, r.y, r.w, r.h, mel_web__rgba(color));
}
void mel_painter_stroke_rect(Mel_Painter* p, Mel_Rect r, Mel_Color color, f32 width)
{
    mel_web__paint_stroke_rect(p->canvas, r.x, r.y, r.w, r.h, mel_web__rgba(color), width);
}
void mel_painter_draw_line(Mel_Painter* p, Mel_Vec2 a, Mel_Vec2 b, Mel_Color color, f32 width)
{
    mel_web__paint_line(p->canvas, a.x, a.y, b.x, b.y, mel_web__rgba(color), width);
}
void mel_painter_fill_round_rect(Mel_Painter* p, Mel_Rect r, f32 radius, Mel_Color color)
{
    mel_web__paint_round_rect(p->canvas, r.x, r.y, r.w, r.h, radius, mel_web__rgba(color));
}
void mel_painter_draw_text(Mel_Painter* p, str8 text, Mel_Vec2 pos, Mel_Color color, f32 size)
{
    char b[1024];
    mel_web__paint_text(p->canvas, mel_web__cstr(text, b, sizeof b), pos.x, pos.y,
                        mel_web__rgba(color), size);
}
