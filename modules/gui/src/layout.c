#include "gui_internal.h"

void mel_gui__layout_free(Mel_Layout* layout)
{
    if (!layout) return;
    mel_dealloc(mel_gui__alloc(), layout);
}

void mel_gui__push_bounds(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;
    mel_gui_set_bounds(h, n->x, n->y, n->width, n->height);
}

void mel_gui__layout_measure(Mel_Gui_Handle h, i32 avail_w, i32 avail_h,
                             i32* out_w, i32* out_h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) { if (out_w) *out_w = 0; if (out_h) *out_h = 0; return; }

    if (n->layout && n->layout->vtable && n->layout->vtable->measure) {
        n->layout->vtable->measure(n->layout, h, avail_w, avail_h, out_w, out_h);
        return;
    }

    i32 pw = n->layoutable.fixed_w ? n->layoutable.fixed_w
           : n->layoutable.preferred_w ? n->layoutable.preferred_w
           : n->width;
    i32 ph = n->layoutable.fixed_h ? n->layoutable.fixed_h
           : n->layoutable.preferred_h ? n->layoutable.preferred_h
           : n->height;

    if (out_w) *out_w = pw;
    if (out_h) *out_h = ph;
}

void mel_gui__layout_arrange(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;

    if (n->layout && n->layout->vtable && n->layout->vtable->arrange) {
        n->layout->vtable->arrange(n->layout, h);
    }
}

void mel_gui_set_layout(Mel_Gui_Handle parent, Mel_Layout* layout)
{
    Mel_Gui_Node* n = mel_gui__node(parent);
    if (!n) return;
    if (n->layout && n->layout != layout) mel_gui__layout_free(n->layout);
    n->layout = layout;
    mel_gui_relayout(parent);
}

void mel_gui_relayout(Mel_Gui_Handle handle)
{
    Mel_Gui_Node* n = mel_gui__node(handle);
    if (!n || !n->layout) return;
    mel_gui__layout_arrange(handle);
}
