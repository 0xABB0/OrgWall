#include "linux.h"

#include <gui/controls/gpu_view.h>

#include <allocator/heap.h>

/* The XCB gpu-view: a child window a Vulkan swapchain renders into. It owns a
 * persistent Mel_Gui_Xcb_Native that mel_gpu_view_surface hands to the gpu vulkan
 * linux lowering (xcb_connection + window). The view's resize callback fires from
 * the ConfigureNotify on its own window, routed through mel_gui__xcb_view_resized
 * (called by the backend event pump). */
typedef struct Mel_Xcb_GpuView
{
    Mel_Gui_Xcb_Native      native;
    Mel_Gui_Handle          handle;
    mel_xcb_window          window;
    Mel_Gpu_View_On         on_;
    Mel_Gui_Pointer_Cb      pointer;
    struct Mel_Xcb_GpuView* next;
} Mel_Xcb_GpuView;

static Mel_Xcb_GpuView* g_views;

void mel_gui__xcb_view_resized(mel_xcb_window window, i32 w, i32 h)
{
    for (Mel_Xcb_GpuView* v = g_views; v; v = v->next)
    {
        if (v->window != window)
            continue;
        if (v->on_.on_resize)
            v->on_.on_resize(v->handle, w, h, mel_gui_user(v->handle));
        return;
    }
}

void mel_gui__xcb_view_pointer(mel_xcb_window window, u8 type, i32 x, i32 y)
{
    for (Mel_Xcb_GpuView* v = g_views; v; v = v->next)
    {
        if (v->window != window)
            continue;
        void* u = mel_gui_user(v->handle);
        if (type == MEL_XCB_BUTTON_PRESS && v->pointer.on_pointer_down)
            v->pointer.on_pointer_down(v->handle, x, y, u);
        else if (type == MEL_XCB_BUTTON_RELEASE && v->pointer.on_pointer_up)
            v->pointer.on_pointer_up(v->handle, x, y, u);
        else if (type == MEL_XCB_MOTION_NOTIFY && v->pointer.on_pointer_move)
            v->pointer.on_pointer_move(v->handle, x, y, u);
        return;
    }
}

void mel_gui__xcb_view_drop(mel_xcb_window window)
{
    Mel_Xcb_GpuView** pp = &g_views;
    for (Mel_Xcb_GpuView* v = g_views; v; pp = &v->next, v = v->next)
    {
        if (v->window != window)
            continue;
        *pp = v->next;
        mel_dealloc(mel_gui__alloc(), v);
        return;
    }
}

Mel_Gui_Handle mel_gpu_view_create_opt(Mel_Gui_Handle parent, Mel_Gpu_View_Opt o)
{
    Mel_Xcb_State* x = mel_gui__xcb();

    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    mel_xcb_window w = mel_gui__xcb_create_child(n, MEL_XCB_EVENT_MASK_BUTTON_PRESS | MEL_XCB_EVENT_MASK_BUTTON_RELEASE | MEL_XCB_EVENT_MASK_POINTER_MOTION);
    n->native = (void*)(uintptr_t)w;

    Mel_Xcb_GpuView* v = mel_alloc_type(mel_gui__alloc(), Mel_Xcb_GpuView);
    *v = (Mel_Xcb_GpuView){ 0 };
    v->handle = h;
    v->window = w;
    v->on_ = o.on_;
    v->pointer = o.pointer;
    v->native.xcb_connection = x->ok ? (void*)x->conn : NULL;
    v->native.xcb_window = w;
    v->next = g_views;
    g_views = v;

    return h;
}

void* mel_gpu_view_surface(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return NULL;
    mel_xcb_window w = (mel_xcb_window)(uintptr_t)n->native;
    for (Mel_Xcb_GpuView* v = g_views; v; v = v->next)
        if (v->window == w)
            return &v->native;
    return NULL;
}
