#pragma once

#include "window.fwd.h"
#include "core.types.h"
#include "string.str8.fwd.h"

typedef struct {
    u32 width;
    u32 height;
    u32 flags;
} Mel_Window_Create_Opt;

Mel_Window_Handle mel_window_create_opt(str8 title, Mel_Window_Create_Opt opt);
#define mel_window_create(title, ...) mel_window_create_opt((title), (Mel_Window_Create_Opt){__VA_ARGS__})

void mel_window_destroy(Mel_Window_Handle handle);

void mel_window_size(Mel_Window_Handle handle, i32* w, i32* h);
void mel_window_size_pixels(Mel_Window_Handle handle, i32* w, i32* h);
u32  mel_window_id(Mel_Window_Handle handle);
u32  mel_window_count(void);

#include <SDL3/SDL.h>
SDL_Window* mel__window_sdl(Mel_Window_Handle handle);
