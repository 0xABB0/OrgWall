#include <platform/platform.h>
#include <platform/hooks.h>
#include "platform_internal.h"

#include <core/platform.h>
#include <allocator/allocator.h>
#include <collection.array/array.h>

#include <assert.h>

typedef struct
{
    u32                         generation;
    bool                        used;
    Mel_Platform_Win32_Msg_Hook win32_cb;
    Mel_Platform_X11_Event_Hook x11_cb;
    void*                       user;
} Hook_Slot;

typedef struct
{
    u32         generation;
    bool        used;
    const char* reason;
    u64         native;
} Inhibit_Slot;

typedef Mel_Array(Hook_Slot) Hook_Array;
typedef Mel_Array(Inhibit_Slot) Inhibit_Array;

typedef struct
{
    const Mel_Alloc* alloc;
    bool             ready;

    Hook_Array    win32_hooks;
    Hook_Array    x11_hooks;
    Inhibit_Array inhibitors;

    void* x11_display;
    void* wayland_display;
    void* wayland_surface;
} Platform_State;

static Platform_State g_state;

void mel_platform_init(const Mel_Alloc* alloc)
{
    assert(alloc != NULL);
    assert(!g_state.ready);
    g_state.alloc = alloc;
    mel_array_init(&g_state.win32_hooks, alloc);
    mel_array_init(&g_state.x11_hooks, alloc);
    mel_array_init(&g_state.inhibitors, alloc);
    g_state.ready = true;
}

void mel_platform_shutdown(void)
{
    if (!g_state.ready)
        return;
    for (usize i = 0; i < g_state.inhibitors.count; i++)
    {
        Inhibit_Slot* s = &g_state.inhibitors.items[i];
        if (s->used)
            mel_platform__backend()->screensaver_uninhibit(s->native);
    }
    mel_array_free(&g_state.win32_hooks);
    mel_array_free(&g_state.x11_hooks);
    mel_array_free(&g_state.inhibitors);
    g_state.x11_display = NULL;
    g_state.wayland_display = NULL;
    g_state.wayland_surface = NULL;
    g_state.ready = false;
}

const Mel_Alloc* mel_platform__alloc(void)
{
    assert(g_state.ready);
    return g_state.alloc;
}

const char* mel_platform_name(void) { return mel_platform__backend()->name(); }

u32 mel_platform_device_class(void) { return mel_platform__backend()->device_class(); }

Mel_Platform_Sandbox mel_platform_sandbox(void) { return mel_platform__backend()->sandbox(); }

static u32 hooks_alloc_slot(Hook_Array* arr)
{
    for (usize i = 0; i < arr->count; i++)
    {
        if (!arr->items[i].used)
            return (u32)i;
    }
    Hook_Slot zero = { 0 };
    mel_array_push(arr, zero);
    return (u32)(arr->count - 1);
}

static Hook_Slot* hooks_resolve(Hook_Array* arr, Mel_Platform_Hook h)
{
    if (h.index == 0 || h.index > arr->count)
        return NULL;
    Hook_Slot* s = &arr->items[h.index - 1];
    if (!s->used || s->generation != h.generation)
        return NULL;
    return s;
}

bool mel_platform_win32_hooks_available(void)
{
#if MEL_PLATFORM_WINDOWS
    return true;
#else
    return false;
#endif
}

Mel_Platform_Hook mel_platform_win32_add_msg_hook(Mel_Platform_Win32_Msg_Hook cb, void* user)
{
    assert(g_state.ready);
    assert(cb != NULL);
    u32        i = hooks_alloc_slot(&g_state.win32_hooks);
    Hook_Slot* s = &g_state.win32_hooks.items[i];
    s->used = true;
    s->generation++;
    s->win32_cb = cb;
    s->user = user;
    return (Mel_Platform_Hook){ i + 1, s->generation };
}

void mel_platform_win32_remove_msg_hook(Mel_Platform_Hook h)
{
    assert(g_state.ready);
    Hook_Slot* s = hooks_resolve(&g_state.win32_hooks, h);
    if (s == NULL)
        return;
    s->used = false;
    s->win32_cb = NULL;
    s->user = NULL;
}

bool mel_platform_win32_dispatch(void* hwnd, u32 msg, u64 wparam, i64 lparam)
{
    assert(g_state.ready);
    for (usize i = 0; i < g_state.win32_hooks.count; i++)
    {
        Hook_Slot* s = &g_state.win32_hooks.items[i];
        if (s->used && s->win32_cb && s->win32_cb(hwnd, msg, wparam, lparam, s->user))
            return true;
    }
    return false;
}

bool mel_platform_x11_hooks_available(void)
{
#if MEL_PLATFORM_LINUX
    return true;
#else
    return false;
#endif
}

Mel_Platform_Hook mel_platform_x11_add_event_hook(Mel_Platform_X11_Event_Hook cb, void* user)
{
    assert(g_state.ready);
    assert(cb != NULL);
    u32        i = hooks_alloc_slot(&g_state.x11_hooks);
    Hook_Slot* s = &g_state.x11_hooks.items[i];
    s->used = true;
    s->generation++;
    s->x11_cb = cb;
    s->user = user;
    return (Mel_Platform_Hook){ i + 1, s->generation };
}

void mel_platform_x11_remove_event_hook(Mel_Platform_Hook h)
{
    assert(g_state.ready);
    Hook_Slot* s = hooks_resolve(&g_state.x11_hooks, h);
    if (s == NULL)
        return;
    s->used = false;
    s->x11_cb = NULL;
    s->user = NULL;
}

bool mel_platform_x11_dispatch(void* xevent)
{
    assert(g_state.ready);
    for (usize i = 0; i < g_state.x11_hooks.count; i++)
    {
        Hook_Slot* s = &g_state.x11_hooks.items[i];
        if (s->used && s->x11_cb && s->x11_cb(xevent, s->user))
            return true;
    }
    return false;
}

void  mel_platform_x11_set_display(void* display) { g_state.x11_display = display; }
void* mel_platform_x11_display(void) { return g_state.x11_display; }

void mel_platform_wayland_set_handles(void* display, void* surface)
{
    g_state.wayland_display = display;
    g_state.wayland_surface = surface;
}

void* mel_platform_wayland_display(void) { return g_state.wayland_display; }
void* mel_platform_wayland_surface(void) { return g_state.wayland_surface; }

bool mel_platform_wayland_available(void) { return g_state.wayland_display != NULL; }

static u32 inhibit_alloc_slot(void)
{
    for (usize i = 0; i < g_state.inhibitors.count; i++)
    {
        if (!g_state.inhibitors.items[i].used)
            return (u32)i;
    }
    Inhibit_Slot zero = { 0 };
    mel_array_push(&g_state.inhibitors, zero);
    return (u32)(g_state.inhibitors.count - 1);
}

Mel_Platform_Inhibit_Result mel_platform_screensaver_inhibit_opt(Mel_Platform_Inhibit_Opt opt)
{
    assert(g_state.ready);
    u32           i = inhibit_alloc_slot();
    Inhibit_Slot* s = &g_state.inhibitors.items[i];
    s->generation++;
    s->reason = opt.reason;
    Mel_Platform_Inhibit_Native n = mel_platform__backend()->screensaver_inhibit(opt.reason);
    if (mel_platform_status_failed(n.status))
        return (Mel_Platform_Inhibit_Result){ MEL_PLATFORM_INHIBITOR_NULL, n.status };
    s->used = true;
    s->native = n.native;
    return (Mel_Platform_Inhibit_Result){ { i + 1, s->generation }, n.status };
}

Mel_Platform_Status mel_platform_screensaver_uninhibit(Mel_Platform_Inhibitor h)
{
    assert(g_state.ready);
    if (h.index == 0 || h.index > g_state.inhibitors.count)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_INVALID;
    Inhibit_Slot* s = &g_state.inhibitors.items[h.index - 1];
    if (!s->used || s->generation != h.generation)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_INVALID;
    Mel_Platform_Status st = mel_platform__backend()->screensaver_uninhibit(s->native);
    s->used = false;
    s->reason = NULL;
    s->native = 0;
    return st;
}

bool mel_platform_screensaver_inhibited(void)
{
    assert(g_state.ready);
    for (usize i = 0; i < g_state.inhibitors.count; i++)
    {
        if (g_state.inhibitors.items[i].used)
            return true;
    }
    return false;
}
