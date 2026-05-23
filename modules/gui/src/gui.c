#include "gui_internal.h"

#include <allocator.heap/heap.h>

static Mel_SlotMap      g_widgets;
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

static bool nonzero(const void* p, usize n)
{
    const u8* b = (const u8*)p;
    for (usize i = 0; i < n; i++) {
        if (b[i] != 0) return true;
    }
    return false;
}

void mel_gui_init(Mel_Reactor* reactor)
{
    if (g_inited) return;
    g_alloc   = mel_alloc_heap();
    g_reactor = reactor;
    mel_slotmap_init(&g_widgets, g_alloc,
        .item_size = sizeof(Mel_Gui_Widget), .initial_capacity = 64);
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

Mel_Gui_Widget* mel_gui__get(Mel_Gui_Handle h)
{
    if (mel_gui_handle_is_none(h)) return NULL;
    return (Mel_Gui_Widget*)mel_slotmap_get(&g_widgets, to_sm(h));
}

bool mel_gui_alive(Mel_Gui_Handle h)
{
    if (mel_gui_handle_is_none(h)) return false;
    return mel_slotmap_alive(&g_widgets, to_sm(h));
}

bool mel_gui__is_toplevel(const Mel_Gui_Widget* w)
{
    return w && mel_gui_handle_is_none(w->parent);
}

Mel_Gui_Widget* mel_gui__widgets(u32* count_out)
{
    if (count_out) *count_out = mel_slotmap_count(&g_widgets);
    return (Mel_Gui_Widget*)mel_slotmap_data(&g_widgets);
}

Mel_Gui_Handle mel_gui__create(Mel_Gui_Handle parent,
                               i32 x, i32 y, i32 width, i32 height, u32 id, void* user,
                               bool disabled, bool hidden, usize impl_size,
                               const Mel_Gui_Lifecycle_Cb* lc,
                               const Mel_Gui_Focus_Cb* fc,
                               const Mel_Gui_Pointer_Cb* pc,
                               const Mel_Gui_Keyboard_Cb* kc,
                               const Mel_Layoutable* layoutable,
                               Mel_Layout* layout)
{
    Mel_Gui_Widget init = {0};
    init.parent = parent;

    Mel_SlotMap_Handle sh = mel_slotmap_insert(&g_widgets, &init);
    Mel_Gui_Handle     h  = from_sm(sh);

    Mel_Gui_Widget* w = (Mel_Gui_Widget*)mel_slotmap_get(&g_widgets, sh);
    if (!w) return MEL_GUI_HANDLE_NONE;

    w->self     = h;
    w->id       = id;
    w->user     = user;
    w->x        = x;
    w->y        = y;
    w->width    = width;
    w->height   = height;
    w->disabled = disabled;
    w->hidden   = hidden;
    if (layoutable) w->layoutable = *layoutable;
    w->layout = layout;

    if (impl_size > 0) {
        w->impl = mel_calloc(mel_gui__alloc(), impl_size);
    }

    bool any = (lc && nonzero(lc, sizeof *lc))
            || (fc && nonzero(fc, sizeof *fc))
            || (pc && nonzero(pc, sizeof *pc))
            || (kc && nonzero(kc, sizeof *kc));

    if (any) {
        Mel_Gui_Callbacks* cb = (Mel_Gui_Callbacks*)mel_calloc(mel_gui__alloc(), sizeof *cb);
        if (cb) {
            if (lc) cb->lifecycle = *lc;
            if (fc) cb->focus     = *fc;
            if (pc) cb->pointer   = *pc;
            if (kc) cb->keyboard  = *kc;
        }
        w->cb = cb;
    }

    return h;
}

static void release_widget(Mel_Gui_Widget* w)
{
    if (w->impl)   mel_dealloc(mel_gui__alloc(), w->impl);
    if (w->cb)     mel_dealloc(mel_gui__alloc(), w->cb);
    if (w->layout) mel_gui__layout_free(w->layout);
}

void mel_gui_destroy(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w) return;

    if (mel_gui__is_toplevel(w)) {
        mel_gui__backend_destroy(w);
        return;
    }

    if (w->cb && w->cb->lifecycle.on_destroy) {
        w->cb->lifecycle.on_destroy(h, w->user);
    }
    mel_gui__backend_destroy(w);
    release_widget(w);
    mel_slotmap_remove(&g_widgets, to_sm(h));
}

void mel_gui__destroy_tree(Mel_Gui_Handle root)
{
    u32 count = mel_slotmap_count(&g_widgets);
    if (count == 0) return;

    Mel_Gui_Handle* hits = (Mel_Gui_Handle*)mel_alloc(mel_gui__alloc(),
                                                      sizeof(Mel_Gui_Handle) * count);
    if (!hits) return;

    Mel_Gui_Widget* data = (Mel_Gui_Widget*)mel_slotmap_data(&g_widgets);
    u32 n = 0;
    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Widget* w = &data[i];
        if (mel_gui_handle_eq(w->self, root) || mel_gui_handle_eq(w->parent, root)) {
            hits[n++] = w->self;
        }
    }

    for (u32 i = 0; i < n; i++) {
        Mel_Gui_Widget* w = mel_gui__get(hits[i]);
        if (!w) continue;
        if (w->cb && w->cb->lifecycle.on_destroy) {
            w->cb->lifecycle.on_destroy(hits[i], w->user);
        }
        release_widget(w);
        mel_slotmap_remove(&g_widgets, to_sm(hits[i]));
    }

    mel_dealloc(mel_gui__alloc(), hits);
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

    u32 count = mel_slotmap_count(&g_widgets);
    Mel_Gui_Widget* data = (Mel_Gui_Widget*)mel_slotmap_data(&g_widgets);

    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Widget* w = &data[i];
        if (w->native) mel_gui__backend_destroy(w);
    }

    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Widget* w = &data[i];
        if (w->impl)   mel_dealloc(g_alloc, w->impl);
        if (w->cb)     mel_dealloc(g_alloc, w->cb);
        if (w->layout) mel_gui__layout_free(w->layout);
        w->impl   = NULL;
        w->cb     = NULL;
        w->layout = NULL;
    }

    mel_slotmap_free(&g_widgets);
    mel_gui__screens_reset();

    g_focused     = MEL_GUI_HANDLE_NONE;
    g_frame_count = 0;
    g_inited      = false;
    g_alloc       = NULL;
    g_reactor     = NULL;
}
