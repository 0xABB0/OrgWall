#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Platform_Hook;

#define MEL_PLATFORM_HOOK_NULL ((Mel_Platform_Hook){ 0, 0 })

static inline bool mel_platform_hook_valid(Mel_Platform_Hook h) { return h.index != 0 || h.generation != 0; }

typedef bool (*Mel_Platform_Win32_Msg_Hook)(void* hwnd, u32 msg, u64 wparam, i64 lparam, void* user);

bool              mel_platform_win32_hooks_available(void);
Mel_Platform_Hook mel_platform_win32_add_msg_hook(Mel_Platform_Win32_Msg_Hook cb, void* user);
void              mel_platform_win32_remove_msg_hook(Mel_Platform_Hook h);
bool              mel_platform_win32_dispatch(void* hwnd, u32 msg, u64 wparam, i64 lparam);

typedef bool (*Mel_Platform_X11_Event_Hook)(void* xevent, void* user);

bool              mel_platform_x11_hooks_available(void);
Mel_Platform_Hook mel_platform_x11_add_event_hook(Mel_Platform_X11_Event_Hook cb, void* user);
void              mel_platform_x11_remove_event_hook(Mel_Platform_Hook h);
bool              mel_platform_x11_dispatch(void* xevent);
void*             mel_platform_x11_display(void);

void* mel_platform_wayland_display(void);
void* mel_platform_wayland_surface(void);
bool  mel_platform_wayland_available(void);

void mel_platform_x11_set_display(void* display);
void mel_platform_wayland_set_handles(void* display, void* surface);

#ifdef __cplusplus
}
#endif
