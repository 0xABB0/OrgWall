#include "gui_internal.h"

#include <allocator/heap.h>

static Mel_SlotMap      g_nodes;
static const Mel_Alloc* g_alloc;
static Mel_Vat*         g_vat;
static bool             g_inited;
static i32              g_frame_count;
static Mel_Gui_Handle   g_focused;

static Mel_SlotMap_Handle to_sm(Mel_Gui_Handle h) { return (Mel_SlotMap_Handle){ .index = h.index, .generation = h.generation }; }

static Mel_Gui_Handle from_sm(Mel_SlotMap_Handle h) { return (Mel_Gui_Handle){ .index = h.index, .generation = h.generation }; }

void mel_gui_init(Mel_Vat* vat)
{
    if (g_inited)
        return;
    g_alloc = mel_alloc_heap();
    g_vat = vat;
    if (vat)
        mel_vat_retain(vat);
    mel_slotmap_init(&g_nodes, g_alloc, .item_size = sizeof(Mel_Gui_Node), .initial_capacity = 64);
    mel_gui__backend_init();
    g_inited = true;
}

const Mel_Alloc* mel_gui__alloc(void) { return g_alloc ? g_alloc : mel_alloc_heap(); }

Mel_Vat* mel_gui__vat(void) { return g_vat; }

bool mel_gui_backend_supports(Mel_Gui_Capability cap)
{
#if MEL_PLATFORM_WINDOWS || MEL_PLATFORM_OSX
    switch (cap)
    {
    case MEL_GUI_CAP_MULTI_WINDOW:
        return true;
    case MEL_GUI_CAP_NATIVE_MENUS:
        return true;
    }
#endif
    (void)cap;
    return false;
}

Mel_Gui_Node* mel_gui__node(Mel_Gui_Handle h)
{
    if (mel_gui_handle_is_none(h))
        return NULL;
    return (Mel_Gui_Node*)mel_slotmap_get(&g_nodes, to_sm(h));
}

Mel_Gui_Node* mel_gui__nodes(u32* count_out)
{
    if (count_out)
        *count_out = mel_slotmap_count(&g_nodes);
    return (Mel_Gui_Node*)mel_slotmap_data(&g_nodes);
}

bool mel_gui_alive(Mel_Gui_Handle h)
{
    if (mel_gui_handle_is_none(h))
        return false;
    return mel_slotmap_alive(&g_nodes, to_sm(h));
}

bool mel_gui__is_toplevel(const Mel_Gui_Node* n) { return n && mel_gui_handle_is_none(n->parent); }

Mel_Gui_Handle mel_gui__first_child(Mel_Gui_Handle parent)
{
    Mel_Gui_Node* n = mel_gui__node(parent);
    return n ? n->first_child : MEL_GUI_HANDLE_NONE;
}

Mel_Gui_Handle mel_gui__next_sibling(Mel_Gui_Handle child)
{
    Mel_Gui_Node* n = mel_gui__node(child);
    return n ? n->next_sibling : MEL_GUI_HANDLE_NONE;
}

u32 mel_gui__child_count(Mel_Gui_Handle parent)
{
    u32 count = 0;
    for (Mel_Gui_Handle c = mel_gui__first_child(parent); !mel_gui_handle_is_none(c); c = mel_gui__next_sibling(c))
        count++;
    return count;
}

static void child_link(Mel_Gui_Node* parent, Mel_Gui_Node* child)
{
    child->prev_sibling = parent->last_child;
    child->next_sibling = MEL_GUI_HANDLE_NONE;
    if (!mel_gui_handle_is_none(parent->last_child))
    {
        Mel_Gui_Node* last = mel_gui__node(parent->last_child);
        if (last)
            last->next_sibling = child->self;
    }
    else
    {
        parent->first_child = child->self;
    }
    parent->last_child = child->self;
}

static void child_unlink(Mel_Gui_Node* n)
{
    Mel_Gui_Node* parent = mel_gui__node(n->parent);
    Mel_Gui_Node* prev = mel_gui__node(n->prev_sibling);
    Mel_Gui_Node* next = mel_gui__node(n->next_sibling);

    if (prev)
        prev->next_sibling = n->next_sibling;
    else if (parent && mel_gui_handle_eq(parent->first_child, n->self))
        parent->first_child = n->next_sibling;

    if (next)
        next->prev_sibling = n->prev_sibling;
    else if (parent && mel_gui_handle_eq(parent->last_child, n->self))
        parent->last_child = n->prev_sibling;

    n->prev_sibling = MEL_GUI_HANDLE_NONE;
    n->next_sibling = MEL_GUI_HANDLE_NONE;
}

Mel_Gui_Handle mel_gui__toplevel(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    while (n && !mel_gui__is_toplevel(n))
        n = mel_gui__node(n->parent);
    return n ? n->self : MEL_GUI_HANDLE_NONE;
}

Mel_Gui_Handle mel_gui__node_new(Mel_Gui_Handle parent, i32 x, i32 y, i32 w, i32 h, u32 id, void* user, bool hidden, const Mel_Layoutable* layoutable, Mel_Layout* layout)
{
    Mel_Gui_Node init = { 0 };
    init.parent = parent;

    Mel_SlotMap_Handle sh = mel_slotmap_insert(&g_nodes, &init);
    Mel_Gui_Handle     handle = from_sm(sh);

    Mel_Gui_Node* n = (Mel_Gui_Node*)mel_slotmap_get(&g_nodes, sh);
    if (!n)
        return MEL_GUI_HANDLE_NONE;

    n->self = handle;
    n->id = id;
    n->user = user;
    n->x = x;
    n->y = y;
    n->width = w;
    n->height = h;
    n->hidden = hidden;
    if (layoutable)
        n->layoutable = *layoutable;
    n->layout = layout;

    Mel_Gui_Node* p = mel_gui__node(parent);
    if (p)
        child_link(p, n);

    return handle;
}

void mel_gui__node_release(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;
    child_unlink(n);
    if (n->layout)
        mel_gui__layout_free(n->layout);
    mel_slotmap_remove(&g_nodes, to_sm(h));
}

void mel_gui_destroy(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;

    if (mel_gui__is_toplevel(n))
    {
        mel_gui__backend_destroy(n);
        return;
    }

    mel_gui__backend_destroy(n);
    mel_gui__node_release(h);
}

void mel_gui__destroy_tree(Mel_Gui_Handle root)
{
    /* Releasing a child unlinks it from the sibling list, so walk a snapshot. */
    u32 count = mel_gui__child_count(root);
    if (count > 0)
    {
        Mel_Gui_Handle* kids = (Mel_Gui_Handle*)mel_alloc(mel_gui__alloc(), sizeof(Mel_Gui_Handle) * count);
        if (kids)
        {
            u32 k = 0;
            for (Mel_Gui_Handle c = mel_gui__first_child(root); !mel_gui_handle_is_none(c); c = mel_gui__next_sibling(c))
                kids[k++] = c;
            for (u32 i = 0; i < k; i++)
                mel_gui__destroy_tree(kids[i]);
            mel_dealloc(mel_gui__alloc(), kids);
        }
    }

    Mel_Gui_Node* node = mel_gui__node(root);
    if (!node)
        return;
    mel_gui__backend_destroy(node);
    mel_gui__node_release(root);
}

void mel_gui__resized(Mel_Gui_Handle h, i32 w, i32 height)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;
    n->width = w;
    n->height = height;
    if (n->layout || n->container_arrange || n->is_scroll_host)
        mel_gui__layout_arrange(h);
    if (mel_gui__is_toplevel(n))
        mel_gui__nav_window_resized(h, w, height);
}

/* Natural content extent of a frame: the layout's measured size, or the bounding
 * box of absolutely-placed children. Pure measurement — no window policy, no
 * constants. Backends that size a Root to its content (desktop) consume this. */
void mel_gui__content_size(Mel_Gui_Handle frame, i32* out_w, i32* out_h)
{
    i32           cw = 0, ch = 0;
    Mel_Gui_Node* fw = mel_gui__node(frame);
    if (!fw)
    {
        if (out_w)
            *out_w = 0;
        if (out_h)
            *out_h = 0;
        return;
    }

    if (fw->layout)
    {
        mel_gui__layout_measure(frame, 0, 0, &cw, &ch);
    }
    else
    {
        for (Mel_Gui_Handle c = mel_gui__first_child(frame); !mel_gui_handle_is_none(c); c = mel_gui__next_sibling(c))
        {
            Mel_Gui_Node* n = mel_gui__node(c);
            if (!n)
                continue;
            i32 rx = n->x + n->width;
            i32 ry = n->y + n->height;
            if (rx > cw)
                cw = rx;
            if (ry > ch)
                ch = ry;
        }
    }

    if (out_w)
        *out_w = cw;
    if (out_h)
        *out_h = ch;
}

void mel_gui__set_focused(Mel_Gui_Handle h) { g_focused = h; }

Mel_Gui_Handle mel_gui_focused(void) { return g_focused; }

i32 mel_gui__frames_inc(void) { return ++g_frame_count; }

i32 mel_gui__frames_dec(void)
{
    if (g_frame_count > 0)
        g_frame_count--;
    return g_frame_count;
}

void mel_gui_shutdown(void)
{
    if (!g_inited)
        return;

    if (g_vat && mel_vat_is_owner(g_vat))
        mel_vat_release(g_vat);

    /* Drop navigation bookkeeping first (it only holds handles, destroys no
     * frames), so the backend teardown below — which fires each frame's OS-close
     * path and thus mel_gui__frame_closed — finds an empty g_navs and no-ops
     * instead of reviving a predecessor mid-shutdown. */
    mel_gui__navs_reset();

    u32           count = mel_slotmap_count(&g_nodes);
    Mel_Gui_Node* data = (Mel_Gui_Node*)mel_slotmap_data(&g_nodes);

    for (u32 i = 0; i < count; i++)
    {
        Mel_Gui_Node* n = &data[i];
        if (n->native)
            mel_gui__backend_destroy(n);
    }

    for (u32 i = 0; i < count; i++)
    {
        Mel_Gui_Node* n = &data[i];
        if (n->layout)
            mel_gui__layout_free(n->layout);
        n->layout = NULL;
    }

    mel_slotmap_free(&g_nodes);
    mel_gui__screens_reset();

    g_focused = MEL_GUI_HANDLE_NONE;
    g_frame_count = 0;
    g_inited = false;
    g_alloc = NULL;
    g_vat = NULL;
}
