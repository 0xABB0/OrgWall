#pragma once

#include "core.types.h"

typedef struct Mel_Render_Manager Mel_Render_Manager;

typedef struct {
    u32 group;
    u32 start;
    u32 count;
} Mel_Mgr_Range;

typedef struct {
    u32 idx;
    u32 gen;
} Mel_Render_Handle;

#define MEL_RENDER_HANDLE_NONE ((Mel_Render_Handle){0})

static inline bool mel_render_handle_valid(Mel_Render_Handle h) { return h.gen != 0; }
static inline bool mel_render_handle_eq(Mel_Render_Handle a, Mel_Render_Handle b)
{
    return a.idx == b.idx && a.gen == b.gen;
}
