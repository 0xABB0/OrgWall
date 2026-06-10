#include "window_internal.h"

#include <allocator/heap.h>

static Mel_SlotMap      g_windows;
static const Mel_Alloc* g_alloc;
static Mel_Window_Host  g_host;
static bool             g_inited;
static bool             g_shutting;
static i32              g_count;

static Mel_SlotMap_Handle to_sm(Mel_Window w) { return (Mel_SlotMap_Handle){ .index = w.index, .generation = w.generation }; }

static Mel_Window from_sm(Mel_SlotMap_Handle h) { return (Mel_Window){ .index = h.index, .generation = h.generation }; }

void mel_window_init(Mel_Window_Host host)
{
    if (g_inited)
        return;
    g_alloc = mel_alloc_heap();
    g_host = host;
    mel_slotmap_init(&g_windows, g_alloc, .item_size = sizeof(Mel_Window_Node), .initial_capacity = 8);
    mel_window__backend_init();
    g_inited = true;
}

void mel_window_shutdown(void)
{
    if (!g_inited)
        return;
    g_shutting = true;

    u32              count = mel_slotmap_count(&g_windows);
    Mel_Window_Node* data = (Mel_Window_Node*)mel_slotmap_data(&g_windows);
    for (u32 i = 0; i < count; i++)
    {
        if (data[i].surface_pixels)
        {
            mel_dealloc(g_alloc, data[i].surface_pixels);
            data[i].surface_pixels = NULL;
        }
        if (data[i].native)
            mel_window__backend_destroy(&data[i]);
    }

    mel_slotmap_free(&g_windows);
    g_inited = false;
    g_shutting = false;
    g_alloc = NULL;
    g_host = (Mel_Window_Host){ 0 };
    g_count = 0;
}

const Mel_Alloc* mel_window__alloc(void) { return g_alloc ? g_alloc : mel_alloc_heap(); }

Mel_Window_Node* mel_window__node(Mel_Window w)
{
    if (mel_window_is_none(w))
        return NULL;
    return (Mel_Window_Node*)mel_slotmap_get(&g_windows, to_sm(w));
}

u32 mel_window__node_count(void) { return mel_slotmap_count(&g_windows); }

Mel_Window_Node* mel_window__node_dense(u32 dense_index)
{
    if (dense_index >= mel_slotmap_count(&g_windows))
        return NULL;
    Mel_Window_Node* data = (Mel_Window_Node*)mel_slotmap_data(&g_windows);
    return &data[dense_index];
}

bool mel_window_alive(Mel_Window w)
{
    if (mel_window_is_none(w))
        return false;
    return mel_slotmap_alive(&g_windows, to_sm(w));
}

i32 mel_window__count_inc(void) { return ++g_count; }

i32 mel_window__count_dec(void)
{
    if (g_count > 0)
        g_count--;
    return g_count;
}

void mel_window_keepalive_inc(void) { mel_window__count_inc(); }

void mel_window_keepalive_dec(void)
{
    if (g_shutting)
        return;
    if (mel_window__count_dec() == 0 && g_host.quit)
        g_host.quit(g_host.user);
}

Mel_Window mel_window_create_opt(Mel_Window_Opt o)
{
    Mel_Window_Node    init = { 0 };
    Mel_SlotMap_Handle sh = mel_slotmap_insert(&g_windows, &init);
    Mel_Window         w = from_sm(sh);

    Mel_Window_Node* n = (Mel_Window_Node*)mel_slotmap_get(&g_windows, sh);
    if (!n)
        return MEL_WINDOW_NONE;

    n->self = w;
    n->user = o.user;
    n->x = o.x;
    n->y = o.y;
    n->w = o.w > 0 ? o.w : 640;
    n->h = o.h > 0 ? o.h : 480;
    n->point_w = n->w;
    n->point_h = n->h;
    n->scale = 1.0f;
    n->min_w = o.min_w;
    n->min_h = o.min_h;
    n->max_w = o.max_w;
    n->max_h = o.max_h;
    n->opacity = 1.0f;
    n->resizable = !o.not_resizable;
    n->borderless = o.undecorated;
    n->fullscreen_flags = MEL_WINDOW_FULLSCREEN_OFF;
    n->progress_state = MEL_WINDOW_PROGRESS_NONE;
    n->ops = mel_window__backend_ops();
    n->lifecycle = o.lifecycle;
    n->display = o.display;
    n->app = o.app;
    n->input = o.input;
    n->backing = o.backing;

    mel_window__backend_create(n, &o);
    mel_window__count_inc();
    return w;
}

void mel_window_destroy(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n)
        return;
    mel_window__backend_destroy(n);
}

void mel_window__resized(Mel_Window w, i32 pixel_w, i32 pixel_h)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n)
        return;
    n->w = pixel_w;
    n->h = pixel_h;
    if (n->lifecycle.on_resize)
        n->lifecycle.on_resize(w, pixel_w, pixel_h, n->user);
}

void mel_window__closed(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n)
        return;

    if (n->lifecycle.on_closed)
        n->lifecycle.on_closed(w, n->user);
    if (n->surface_pixels)
    {
        mel_dealloc(g_alloc, n->surface_pixels);
        n->surface_pixels = NULL;
    }
    if (g_shutting)
        return;

    mel_slotmap_remove(&g_windows, to_sm(w));
    if (mel_window__count_dec() == 0 && g_host.quit)
        g_host.quit(g_host.user);
}

void* mel_window_content_native(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    return n ? n->content : NULL;
}

void* mel_window_native(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    return n ? n->native : NULL;
}

void mel_window_pixel_extent(Mel_Window w, i32* out_w, i32* out_h)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (out_w)
        *out_w = n ? n->w : 0;
    if (out_h)
        *out_h = n ? n->h : 0;
}

void mel_window_point_extent(Mel_Window w, i32* out_w, i32* out_h)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (out_w)
        *out_w = n ? n->point_w : 0;
    if (out_h)
        *out_h = n ? n->point_h : 0;
}

f32 mel_window_scale(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    return n ? n->scale : 1.0f;
}
