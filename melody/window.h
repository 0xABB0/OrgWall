#pragma once

#include "window.fwd.h"
#include "core.types.h"
#include "allocator.fwd.h"
#include "string.str8.fwd.h"

#include <SDL3/SDL.h>

struct Mel_Window {
    SDL_Window* sdl;
};

typedef struct {
    u32 width;
    u32 height;
    u32 flags;
} Mel_Window_Create_Opt;

Mel_Window_Handle mel_window_create_opt(str8 title, Mel_Window_Create_Opt opt);
#define mel_window_create(title, ...) mel_window_create_opt((title), (Mel_Window_Create_Opt){__VA_ARGS__})

void mel_window_destroy(Mel_Window_Handle handle);

Mel_Window* mel_window_get(Mel_Window_Handle handle);
u32         mel_window_count(void);
