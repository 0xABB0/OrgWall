#pragma once

#include <core/types.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>

// A layer-hosting view that a GPU swapchain renders into. It is a regular GUI
// component: it lives inside a window beside ordinary widgets, owns no window of
// its own. The GPU module attaches its drawable surface to the native handle
// returned by mel_gpu_view_surface, keeping gui and gpu free of any direct
// dependency on one another.
typedef struct {
    void (*on_resize)(Mel_Gui_Handle h, i32 w, i32 height, void* user);
} Mel_Gpu_View_On;

typedef struct {
    i32   x, y, w, h;
    u32   id;
    bool  hidden;
    void* user;
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Pointer_Cb   pointer;
    Mel_Gui_Keyboard_Cb  keyboard;
    Mel_Gpu_View_On      on_;
    Mel_Layoutable       layoutable;
} Mel_Gpu_View_Opt;

Mel_Gui_Handle mel_gpu_view_create_opt(Mel_Gui_Handle parent, Mel_Gpu_View_Opt opt);
#define mel_gpu_view_create(parent, ...) \
    mel_gpu_view_create_opt((parent), (Mel_Gpu_View_Opt){__VA_ARGS__})

// Native surface handle to hand to mel_gpu_swapchain_create (an NSView* on
// macOS), returned as an opaque void* so callers stay in plain C.
void* mel_gpu_view_surface(Mel_Gui_Handle h);
