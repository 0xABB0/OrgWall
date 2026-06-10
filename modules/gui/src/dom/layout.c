#include "web.h"

#include <layout/linear.h>
#include <layout/stack.h>

// Linear layouts lower to CSS flex, stack layouts to a single-cell CSS grid
// (every item in cell 1/1, stretched). The browser then owns arrangement; a
// shared ResizeObserver mirrors every flexed element's size back into the C
// node so canvas repaint, gpu on_resize and content_size read true values.

EM_JS(void, mel_web__flex_host, (int id, int vertical, int gap, int pad, const char* align), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    el.style.display = 'flex';
    el.dataset.melDisplay = 'flex';
    el.style.flexDirection = vertical ? 'column' : 'row';
    el.style.gap = gap + 'px';
    el.style.padding = pad + 'px';
    el.style.alignItems = UTF8ToString(align);
});

EM_JS(void, mel_web__grid_stack_host, (int id, int pad), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    el.style.display = 'grid';
    el.dataset.melDisplay = 'grid';
    el.style.padding = pad + 'px';
});

EM_JS(void, mel_web__flex_item_set, (int id, int grow, int basis, int cross, int vertical, const char* align_self, int ml, int mt, int mr, int mb), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    el.style.position = 'static';
    el.style.left = '';
    el.style.top = '';
    el.style.flexGrow = '' + grow;
    el.style.flexShrink = '0';
    el.style.flexBasis = basis > 0 ? basis + 'px' : 'auto';
    if (vertical)
    {
        el.style.height = '';
        el.style.width = cross > 0 ? cross + 'px' : '';
    }
    else
    {
        el.style.width = '';
        el.style.height = cross > 0 ? cross + 'px' : '';
    }
    const a = UTF8ToString(align_self);
    if (a)
        el.style.alignSelf = a;
    el.style.margin = mt + 'px ' + mr + 'px ' + mb + 'px ' + ml + 'px';
});

EM_JS(void, mel_web__grid_item_set, (int id, int ml, int mt, int mr, int mb), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    el.style.position = 'static';
    el.style.left = '';
    el.style.top = '';
    el.style.width = '';
    el.style.height = '';
    el.style.gridArea = '1 / 1';
    el.style.margin = mt + 'px ' + mr + 'px ' + mb + 'px ' + ml + 'px';
});

// A lowered scroll host's inner div flows with its content instead of a fixed
// px extent (the core skips set_content_size for lowered nodes); the outer
// keeps overflow:auto.
EM_JS(void, mel_web__scroll_inner_flow, (int id, int vertical), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    el.style.position = 'static';
    el.style.left = '';
    el.style.top = '';
    if (vertical)
    {
        el.style.width = '100%';
        el.style.height = '';
        el.style.minHeight = '100%';
    }
    else
    {
        el.style.height = '100%';
        el.style.width = '';
        el.style.minWidth = '100%';
    }
});

EM_JS(void, mel_web__observe, (int id), {
    if (!MelWeb.ro)
    {
        MelWeb.roIds = new Map();
        MelWeb.ro = new ResizeObserver((entries) => {
            for (const e of entries)
            {
                const eid = MelWeb.roIds.get(e.target);
                if (eid)
                    _mel_web__ev_resize(eid, e.target.offsetWidth | 0, e.target.offsetHeight | 0);
            }
        });
    }
    const el = MelWeb.els[id];
    if (el && !MelWeb.roIds.has(el))
    {
        MelWeb.roIds.set(el, id);
        MelWeb.ro.observe(el);
    }
});

static const char* align_css(u8 a)
{
    if (a == MEL_ALIGN_CENTER)
        return "center";
    if (a == MEL_ALIGN_END)
        return "flex-end";
    if (a == MEL_ALIGN_STRETCH)
        return "stretch";
    return "flex-start";
}

static void member_apply(const Mel_Layout* layout, Mel_Gui_Node* cn)
{
    int id = cn ? (int)(intptr_t)cn->native : 0;
    if (!id)
        return;
    const Mel_Layoutable* l = &cn->layoutable;
    if (layout->cls == mel_linear_layout_class())
    {
        const Mel_Linear_Layout* ll = (const Mel_Linear_Layout*)layout;
        i32                      basis = ll->vertical ? (l->fixed_h ? l->fixed_h : l->preferred_h) : (l->fixed_w ? l->fixed_w : l->preferred_w);
        i32                      cross = ll->vertical ? (l->fixed_w ? l->fixed_w : l->preferred_w) : (l->fixed_h ? l->fixed_h : l->preferred_h);
        u8                       eff = l->cross_align ? l->cross_align : ll->cross_align;
        if (eff == MEL_ALIGN_STRETCH)
            cross = 0;
        mel_web__flex_item_set(id, l->weight, basis, cross, ll->vertical, l->cross_align ? align_css(l->cross_align) : "", l->margin_l, l->margin_t, l->margin_r, l->margin_b);
    }
    else
    {
        mel_web__grid_item_set(id, l->margin_l, l->margin_t, l->margin_r, l->margin_b);
    }
    mel_web__observe(id);
}

void mel_web__member_sync(Mel_Gui_Handle h)
{
    Mel_Gui_Node* cn = mel_gui__node(h);
    if (!cn)
        return;
    Mel_Gui_Node* p = mel_gui__node(cn->parent);
    if (p && p->lowered && p->layout)
        member_apply(p->layout, cn);
}

bool mel_gui__backend_layout_adopt(Mel_Gui_Node* n, Mel_Layout* layout)
{
    if (!n || !layout || !layout->cls)
        return false;
    bool linear = layout->cls == mel_linear_layout_class();
    bool stack = layout->cls == mel_stack_layout_class();
    if (!linear && !stack)
        return false;
    int host = (int)(intptr_t)(n->content ? n->content : n->native);
    if (!host)
        return false;

    if (linear)
    {
        const Mel_Linear_Layout* ll = (const Mel_Linear_Layout*)layout;
        mel_web__flex_host(host, ll->vertical, ll->spacing, ll->margin, align_css(ll->cross_align));
        if (n->is_scroll_host)
            mel_web__scroll_inner_flow(host, ll->vertical);
    }
    else
    {
        const Mel_Stack_Layout* sl = (const Mel_Stack_Layout*)layout;
        mel_web__grid_stack_host(host, sl->margin);
        if (n->is_scroll_host)
            mel_web__scroll_inner_flow(host, 1);
    }

    if (n->native)
        mel_web__observe((int)(intptr_t)n->native);
    for (Mel_Gui_Handle c = mel_gui__first_child(n->self); !mel_gui_handle_is_none(c); c = mel_gui__next_sibling(c))
        member_apply(layout, mel_gui__node(c));
    return true;
}

EMSCRIPTEN_KEEPALIVE void mel_web__ev_resize(int id, int w, int h)
{
    Mel_Web_Ctl* c = mel_web__ctl(id);
    if (!c)
        return;
    Mel_Gui_Node* n = mel_gui__node(c->handle);
    if (!n || (n->width == w && n->height == h))
        return;
    n->width = w;
    n->height = h;
    if (c->canvas.on_paint)
        mel_web__canvas_repaint(n); // re-syncs the bitmap to the flexed size
    if (c->gpu_view.on_resize)
        c->gpu_view.on_resize(c->handle, w, h, mel_gui_user(c->handle));
    if (n->layout || n->container_arrange || n->is_scroll_host)
        mel_gui__layout_arrange(c->handle);
}

bool mel_gui__backend_natural_size(Mel_Gui_Node* n, i32* out_w, i32* out_h)
{
    (void)n;
    (void)out_w;
    (void)out_h;
    return false;
}
