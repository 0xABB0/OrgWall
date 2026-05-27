#pragma once

#include <gpu/gpu.h>

// A graphical application hosted inside a GPU window. The host owns the window,
// the device, the swapchain and the per-frame begin/end; an app only sets up its
// own resources and records one render pass per frame.
typedef struct {
    const char* title;
    void* (*init)    (Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc);
    void  (*resize)  (void* state, i32 w, i32 h);
    void  (*render)  (void* state, Mel_Gpu_Command_List* cmd, f64 dt_seconds);
    void  (*teardown)(void* state);
} Graphical_App;
