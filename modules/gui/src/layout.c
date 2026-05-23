#include "gui_internal.h"

void mel_gui__layout_free(Mel_Layout* layout)
{
    if (!layout) return;
    mel_dealloc(mel_gui__alloc(), layout);
}

void mel_gui__push_bounds(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return;
    mel_gui__backend_set_bounds(w, w->x, w->y, w->width, w->height);
}

void mel_gui__layout_measure(Mel_Gui_Handle h, i32 avail_w, i32 avail_h,
                             i32* out_w, i32* out_h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) { if (out_w) *out_w = 0; if (out_h) *out_h = 0; return; }

    if (w->layout && w->layout->vtable && w->layout->vtable->measure) {
        w->layout->vtable->measure(w->layout, h, avail_w, avail_h, out_w, out_h);
        return;
    }

    i32 pw = w->layoutable.fixed_w ? w->layoutable.fixed_w
           : w->layoutable.preferred_w ? w->layoutable.preferred_w
           : w->width;
    i32 ph = w->layoutable.fixed_h ? w->layoutable.fixed_h
           : w->layoutable.preferred_h ? w->layoutable.preferred_h
           : w->height;

    if (out_w) *out_w = pw;
    if (out_h) *out_h = ph;
}

void mel_gui__layout_arrange(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return;

    if (w->layout && w->layout->vtable && w->layout->vtable->arrange) {
        w->layout->vtable->arrange(w->layout, h);
    }
}

void mel_gui_set_layout(Mel_Gui_Handle parent, Mel_Layout* layout)
{
    Mel_Gui_Widget* w = mel_gui__get(parent);
    if (!w) return;
    if (w->layout && w->layout != layout) mel_gui__layout_free(w->layout);
    w->layout = layout;
    mel_gui_relayout(parent);
}

void mel_gui_relayout(Mel_Gui_Handle handle)
{
    Mel_Gui_Widget* w = mel_gui__get(handle);
    if (!w || !w->layout) return;
    mel_gui__layout_arrange(handle);
}
