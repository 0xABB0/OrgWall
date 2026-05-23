#include "gui_internal.h"

#include <gui/layouts/column.h>

typedef struct {
    Mel_Layout base;
    i32        spacing;
    i32        margin;
    u8         cross_align;
} Mel_Column_Layout;

static bool is_child_of(const Mel_Gui_Widget* child, Mel_Gui_Handle parent)
{
    return mel_gui_handle_eq(child->parent, parent);
}

static u8 resolve_cross(u8 child_align, u8 default_align)
{
    if (child_align == MEL_ALIGN_DEFAULT) {
        return default_align == MEL_ALIGN_DEFAULT ? MEL_ALIGN_START : default_align;
    }
    return child_align;
}

static void child_preferred(const Mel_Gui_Widget* c, i32* pw, i32* ph)
{
    *pw = c->layoutable.fixed_w ? c->layoutable.fixed_w
        : c->layoutable.preferred_w ? c->layoutable.preferred_w
        : c->width;
    *ph = c->layoutable.fixed_h ? c->layoutable.fixed_h
        : c->layoutable.preferred_h ? c->layoutable.preferred_h
        : c->height;
}

static void column_measure(Mel_Layout* layout, Mel_Gui_Handle container,
                           i32 avail_w, i32 avail_h, i32* out_w, i32* out_h)
{
    Mel_Column_Layout* col = (Mel_Column_Layout*)layout;
    (void)avail_w; (void)avail_h;

    u32 count = 0;
    Mel_Gui_Widget* data = mel_gui__widgets(&count);

    i32 primary = 0;
    i32 cross   = 0;
    i32 visible_count = 0;

    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Widget* c = &data[i];
        if (!is_child_of(c, container) || c->hidden) continue;

        i32 pw, ph;
        child_preferred(c, &pw, &ph);

        pw += c->layoutable.margin_l + c->layoutable.margin_r;
        ph += c->layoutable.margin_t + c->layoutable.margin_b;

        primary += ph;
        if (pw > cross) cross = pw;
        visible_count++;
    }

    if (visible_count > 1) primary += col->spacing * (visible_count - 1);
    primary += col->margin * 2;
    cross   += col->margin * 2;

    if (out_w) *out_w = cross;
    if (out_h) *out_h = primary;
}

static void column_arrange(Mel_Layout* layout, Mel_Gui_Handle container)
{
    Mel_Column_Layout* col = (Mel_Column_Layout*)layout;
    Mel_Gui_Widget*    parent = mel_gui__get(container);
    if (!parent) return;

    u32 count = 0;
    Mel_Gui_Widget* data = mel_gui__widgets(&count);

    i32 fixed_primary  = 0;
    i32 total_weight   = 0;
    i32 visible_count  = 0;

    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Widget* c = &data[i];
        if (!is_child_of(c, container) || c->hidden) continue;

        i32 pw, ph;
        child_preferred(c, &pw, &ph);
        fixed_primary += ph + c->layoutable.margin_t + c->layoutable.margin_b;
        total_weight  += c->layoutable.weight;
        visible_count++;
    }

    if (visible_count > 1) fixed_primary += col->spacing * (visible_count - 1);

    i32 container_h = parent->height - col->margin * 2;
    i32 container_w = parent->width  - col->margin * 2;
    i32 leftover    = container_h - fixed_primary;
    if (leftover < 0) leftover = 0;

    i32 cursor = col->margin;

    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Widget* c = &data[i];
        if (!is_child_of(c, container) || c->hidden) continue;

        i32 pw, ph;
        child_preferred(c, &pw, &ph);

        i32 ml = c->layoutable.margin_l;
        i32 mt = c->layoutable.margin_t;
        i32 mr = c->layoutable.margin_r;
        i32 mb = c->layoutable.margin_b;

        i32 extra = 0;
        if (total_weight > 0 && c->layoutable.weight > 0) {
            extra = (leftover * c->layoutable.weight) / total_weight;
        }

        i32 child_h = ph + extra;
        i32 y = cursor + mt;

        u8  align = resolve_cross(c->layoutable.cross_align, col->cross_align);
        i32 avail_cross = container_w - ml - mr;
        i32 child_w = pw;
        i32 x = col->margin + ml;

        switch (align) {
            case MEL_ALIGN_STRETCH:
                child_w = avail_cross;
                break;
            case MEL_ALIGN_CENTER:
                x += (avail_cross - pw) / 2;
                break;
            case MEL_ALIGN_END:
                x += avail_cross - pw;
                break;
            case MEL_ALIGN_START:
            default:
                break;
        }

        c->x      = x;
        c->y      = y;
        c->width  = child_w;
        c->height = child_h;
        mel_gui__push_bounds(c->self);
        if (c->layout) mel_gui__layout_arrange(c->self);

        cursor += mt + child_h + mb + col->spacing;
    }
}

static const Mel_Layout_Vtable s_column_vtable = {
    .measure = column_measure,
    .arrange = column_arrange,
};

Mel_Layout* mel_column_layout_opt(Mel_Column_Layout_Opt opt)
{
    Mel_Column_Layout* col = (Mel_Column_Layout*)mel_calloc(mel_gui__alloc(), sizeof *col);
    if (!col) return NULL;
    col->base.vtable = &s_column_vtable;
    col->spacing     = opt.spacing;
    col->margin      = opt.margin;
    col->cross_align = opt.cross_align;
    return &col->base;
}
