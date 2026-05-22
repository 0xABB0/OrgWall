#pragma once

#include <core/types.h>
#include <string/str8.h>

#include "handle.h"
#include "callbacks.h"

typedef struct {
    str8  title;
    i32   x, y, w, h;
    void* user;
    Mel_Gui_Lifecycle_Cb lifecycle;
} Mel_Frame_Opt;

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt opt);
#define mel_frame_create(...) mel_frame_create_opt((Mel_Frame_Opt){__VA_ARGS__})
