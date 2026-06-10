#include "gui_internal.h"

void mel_gui__layout_free(Mel_Layout* layout)
{
    if (!layout)
        return;
    mel_dealloc(mel_gui__alloc(), layout);
}

void mel_gui__push_bounds(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;
    mel_gui_set_bounds(h, n->x, n->y, n->width, n->height);
}

/* Snapshot the container's visible children as solver items. The arrays are
 * transient: allocated per measure/arrange, freed by the caller. */
static u32 gather_items(Mel_Gui_Handle container, Mel_Layout_Item** out_items, Mel_Gui_Handle** out_kids)
{
    u32 visible = 0;
    for (Mel_Gui_Handle c = mel_gui__first_child(container); !mel_gui_handle_is_none(c); c = mel_gui__next_sibling(c))
    {
        Mel_Gui_Node* cn = mel_gui__node(c);
        if (cn && !cn->hidden)
            visible++;
    }

    *out_items = NULL;
    *out_kids = NULL;
    if (!visible)
        return 0;

    Mel_Layout_Item* items = (Mel_Layout_Item*)mel_alloc(mel_gui__alloc(), sizeof(Mel_Layout_Item) * visible);
    Mel_Gui_Handle*  kids = (Mel_Gui_Handle*)mel_alloc(mel_gui__alloc(), sizeof(Mel_Gui_Handle) * visible);
    if (!items || !kids)
    {
        if (items)
            mel_dealloc(mel_gui__alloc(), items);
        if (kids)
            mel_dealloc(mel_gui__alloc(), kids);
        return 0;
    }

    u32 k = 0;
    for (Mel_Gui_Handle c = mel_gui__first_child(container); !mel_gui_handle_is_none(c); c = mel_gui__next_sibling(c))
    {
        Mel_Gui_Node* cn = mel_gui__node(c);
        if (!cn || cn->hidden)
            continue;

        Mel_Layout_Item* it = &items[k];
        *it = (Mel_Layout_Item){ 0 };
        it->spec = cn->layoutable;
        it->natural_w = cn->width;
        it->natural_h = cn->height;
        if (!it->natural_w || !it->natural_h)
        {
            i32 nw = 0, nh = 0;
            if (mel_gui__backend_natural_size(cn, &nw, &nh))
            {
                if (!it->natural_w)
                    it->natural_w = nw;
                if (!it->natural_h)
                    it->natural_h = nh;
            }
        }
        kids[k] = c;
        k++;
    }

    *out_items = items;
    *out_kids = kids;
    return k;
}

static void free_items(Mel_Layout_Item* items, Mel_Gui_Handle* kids)
{
    if (items)
        mel_dealloc(mel_gui__alloc(), items);
    if (kids)
        mel_dealloc(mel_gui__alloc(), kids);
}

void mel_gui__layout_measure(Mel_Gui_Handle h, i32 avail_w, i32 avail_h, i32* out_w, i32* out_h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
    {
        if (out_w)
            *out_w = 0;
        if (out_h)
            *out_h = 0;
        return;
    }

    if (n->layout && n->layout->cls && n->layout->cls->measure)
    {
        Mel_Layout_Item* items = NULL;
        Mel_Gui_Handle*  kids = NULL;
        u32              count = gather_items(h, &items, &kids);
        n->layout->cls->measure(n->layout, items, count, avail_w, avail_h, out_w, out_h);
        free_items(items, kids);
        return;
    }

    i32 pw = n->layoutable.fixed_w ? n->layoutable.fixed_w : n->layoutable.preferred_w ? n->layoutable.preferred_w : n->width;
    i32 ph = n->layoutable.fixed_h ? n->layoutable.fixed_h : n->layoutable.preferred_h ? n->layoutable.preferred_h : n->height;

    if (out_w)
        *out_w = pw;
    if (out_h)
        *out_h = ph;
}

static bool arranges_children(const Mel_Gui_Node* n) { return n->layout || n->is_scroll_host || n->container_arrange; }

static void arrange_with_class(Mel_Gui_Handle h, Mel_Gui_Node* n, i32 avail_w, i32 avail_h)
{
    Mel_Layout_Item* items = NULL;
    Mel_Gui_Handle*  kids = NULL;
    u32              count = gather_items(h, &items, &kids);
    if (!count)
        return;

    n->layout->cls->arrange(n->layout, items, count, avail_w, avail_h);

    for (u32 i = 0; i < count; i++)
    {
        Mel_Gui_Node* cn = mel_gui__node(kids[i]);
        if (!cn)
            continue;
        cn->x = items[i].x;
        cn->y = items[i].y;
        cn->width = items[i].w;
        cn->height = items[i].h;
        mel_gui__push_bounds(kids[i]);
        if (arranges_children(cn))
            mel_gui__layout_arrange(kids[i]);
    }

    free_items(items, kids);
}

void mel_gui__layout_arrange(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;

    if (n->is_scroll_host)
    {
        mel_gui__scroll_fit(h);
        return;
    }

    if (n->container_arrange)
    {
        n->container_arrange(h);
        return;
    }

    if (n->lowered)
        return;

    if (n->layout && n->layout->cls && n->layout->cls->arrange)
        arrange_with_class(h, n, n->width, n->height);
}

void mel_gui__scroll_fit(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;

    if (n->lowered)
        return; /* the native engine sizes the scrollable content */

    i32 cw = 0, ch = 0;
    mel_gui__content_size(h, &cw, &ch);

    if (cw < n->width)
        cw = n->width;
    if (ch < n->height)
        ch = n->height;
    if (n->content_floor_w > cw)
        cw = n->content_floor_w;
    if (n->content_floor_h > ch)
        ch = n->content_floor_h;

    mel_gui__backend_set_content_size(n, cw, ch);

    if (n->layout && n->layout->cls && n->layout->cls->arrange)
        arrange_with_class(h, n, n->width, ch);
}

void mel_gui__node_native_ready(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (n && n->layout && !n->lowered)
        n->lowered = mel_gui__backend_layout_adopt(n, n->layout);
}

void mel_gui_set_layout(Mel_Gui_Handle parent, Mel_Layout* layout)
{
    Mel_Gui_Node* n = mel_gui__node(parent);
    if (!n)
        return;
    if (n->layout && n->layout != layout)
        mel_gui__layout_free(n->layout);
    n->layout = layout;
    n->lowered = false;
    if (layout && n->native)
        n->lowered = mel_gui__backend_layout_adopt(n, layout);
    if (!n->lowered)
        mel_gui_relayout(parent);
}

void mel_gui_relayout(Mel_Gui_Handle handle)
{
    Mel_Gui_Node* n = mel_gui__node(handle);
    if (!n || n->lowered)
        return;
    if (!arranges_children(n))
        return;
    mel_gui__layout_arrange(handle);
}
