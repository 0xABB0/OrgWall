#pragma once

#include <gpu.h>

typedef struct
{
    const char* title;
    void* (*init)(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* sc);
    void (*resize)(void* state, i32 w, i32 h);
    void (*render)(void* state, Mel_Gpu_Command_List* cmd, f64 dt_seconds);
    void (*teardown)(void* state);
} Graphical_App;
