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

static inline u64 mel_render_handle_pack64(Mel_Render_Handle h)
{
    return ((u64)h.gen << 32) | (u64)h.idx;
}

static inline Mel_Render_Handle mel_render_handle_unpack64(u64 v)
{
    return (Mel_Render_Handle){ .idx = (u32)(v & 0xFFFFFFFF), .gen = (u32)(v >> 32) };
}
