#include "gui_internal.h"

#include <allocator/heap.h>

static Mel_SlotMap      g_nodes;
static const Mel_Alloc* g_alloc;
static Mel_Reactor*     g_reactor;
static bool             g_inited;
static i32              g_frame_count;
static Mel_Gui_Handle   g_focused;

static Mel_SlotMap_Handle to_sm(Mel_Gui_Handle h)
{
    return (Mel_SlotMap_Handle){ .index = h.index, .generation = h.generation };
}

static Mel_Gui_Handle from_sm(Mel_SlotMap_Handle h)
{
    return (Mel_Gui_Handle){ .index = h.index, .generation = h.generation };
}

void mel_gui_init(Mel_Reactor* reactor)
{
    if (g_inited) return;
    g_alloc   = mel_alloc_heap();
    g_reactor = reactor;
    mel_slotmap_init(&g_nodes, g_alloc,
        .item_size = sizeof(Mel_Gui_Node), .initial_capacity = 64);
    mel_gui__backend_init();
    g_inited = true;
}

const Mel_Alloc* mel_gui__alloc(void)
{
    return g_alloc ? g_alloc : mel_alloc_heap();
}

Mel_Reactor* mel_gui__reactor(void)
{
    return g_reactor;
}

bool mel_gui_backend_supports(Mel_Gui_Capability cap)
{
#if MEL_PLATFORM_WINDOWS || MEL_PLATFORM_OSX
    switch (cap) {
        case MEL_GUI_CAP_MULTI_WINDOW: return true;
        case MEL_GUI_CAP_NATIVE_MENUS: return true;
    }
#endif
    (void)cap;
    return false;
}

Mel_Gui_Node* mel_gui__node(Mel_Gui_Handle h)
{
    if (mel_gui_handle_is_none(h)) return NULL;
    return (Mel_Gui_Node*)mel_slotmap_get(&g_nodes, to_sm(h));
}

Mel_Gui_Node* mel_gui__nodes(u32* count_out)
{
    if (count_out) *count_out = mel_slotmap_count(&g_nodes);
    return (Mel_Gui_Node*)mel_slotmap_data(&g_nodes);
}

bool mel_gui_alive(Mel_Gui_Handle h)
{
    if (mel_gui_handle_is_none(h)) return false;
    return mel_slotmap_alive(&g_nodes, to_sm(h));
}

bool mel_gui__is_toplevel(const Mel_Gui_Node* n)
{
    return n && mel_gui_handle_is_none(n->parent);
}

Mel_Gui_Handle mel_gui__toplevel(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    while (n && !mel_gui__is_toplevel(n)) n = mel_gui__node(n->parent);
    return n ? n->self : MEL_GUI_HANDLE_NONE;
}

Mel_Gui_Handle mel_gui__node_new(Mel_Gui_Handle parent,
                                 i32 x, i32 y, i32 w, i32 h, u32 id, void* user,
                                 bool hidden,
                                 const Mel_Layoutable* layoutable, Mel_Layout* layout)
{
    Mel_Gui_Node init = {0};
    init.parent = parent;

    Mel_SlotMap_Handle sh = mel_slotmap_insert(&g_nodes, &init);
    Mel_Gui_Handle     handle = from_sm(sh);

    Mel_Gui_Node* n = (Mel_Gui_Node*)mel_slotmap_get(&g_nodes, sh);
    if (!n) return MEL_GUI_HANDLE_NONE;

    n->self   = handle;
    n->id     = id;
    n->user   = user;
    n->x      = x;
    n->y      = y;
    n->width  = w;
    n->height = h;
    n->hidden = hidden;
    if (layoutable) n->layoutable = *layoutable;
    n->layout = layout;

    return handle;
}

void mel_gui__node_release(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;
    if (n->layout) mel_gui__layout_free(n->layout);
    mel_slotmap_remove(&g_nodes, to_sm(h));
}

void mel_gui_destroy(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;

    if (mel_gui__is_toplevel(n)) {
        mel_gui__backend_destroy(n);
        return;
    }

    mel_gui__backend_destroy(n);
    mel_gui__node_release(h);
}

void mel_gui__destroy_tree(Mel_Gui_Handle root)
{
    u32 count = mel_slotmap_count(&g_nodes);
    if (count == 0) return;

    Mel_Gui_Handle* hits = (Mel_Gui_Handle*)mel_alloc(mel_gui__alloc(),
                                                      sizeof(Mel_Gui_Handle) * count);
    if (!hits) return;

    Mel_Gui_Node* data = (Mel_Gui_Node*)mel_slotmap_data(&g_nodes);
    u32 n = 0;
    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Node* node = &data[i];
        if (mel_gui_handle_eq(node->self, root) || mel_gui_handle_eq(node->parent, root)) {
            hits[n++] = node->self;
        }
    }

    for (u32 i = 0; i < n; i++) {
        Mel_Gui_Node* node = mel_gui__node(hits[i]);
        if (!node) continue;
        mel_gui__backend_destroy(node);
        mel_gui__node_release(hits[i]);
    }

    mel_dealloc(mel_gui__alloc(), hits);
}

void mel_gui__resized(Mel_Gui_Handle h, i32 w, i32 height)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;
    n->width  = w;
    n->height = height;
    if (n->layout) mel_gui__layout_arrange(h);
}

void mel_gui__set_focused(Mel_Gui_Handle h)
{
    g_focused = h;
}

Mel_Gui_Handle mel_gui_focused(void)
{
    return g_focused;
}

i32 mel_gui__frames_inc(void)
{
    return ++g_frame_count;
}

i32 mel_gui__frames_dec(void)
{
    if (g_frame_count > 0) g_frame_count--;
    return g_frame_count;
}

void mel_gui_shutdown(void)
{
    if (!g_inited) return;

    u32           count = mel_slotmap_count(&g_nodes);
    Mel_Gui_Node* data  = (Mel_Gui_Node*)mel_slotmap_data(&g_nodes);

    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Node* n = &data[i];
        if (n->native) mel_gui__backend_destroy(n);
    }

    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Node* n = &data[i];
        if (n->layout) mel_gui__layout_free(n->layout);
        n->layout = NULL;
    }

    mel_slotmap_free(&g_nodes);
    mel_gui__screens_reset();

    g_focused     = MEL_GUI_HANDLE_NONE;
    g_frame_count = 0;
    g_inited      = false;
    g_alloc       = NULL;
    g_reactor     = NULL;
}
