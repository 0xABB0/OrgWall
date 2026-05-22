#pragma once

#include <core/types.h>

typedef struct { u32 index; u32 generation; } Mel_Gui_Handle;

#define MEL_GUI_HANDLE_NONE ((Mel_Gui_Handle){0})

static inline bool mel_gui_handle_is_none(Mel_Gui_Handle h)
{
    return h.index == 0 && h.generation == 0;
}

static inline bool mel_gui_handle_eq(Mel_Gui_Handle a, Mel_Gui_Handle b)
{
    return a.index == b.index && a.generation == b.generation;
}

static inline u64 mel_gui_handle_pack(Mel_Gui_Handle h)
{
    return ((u64)h.generation << 32) | (u64)h.index;
}

static inline Mel_Gui_Handle mel_gui_handle_unpack(u64 packed)
{
    return (Mel_Gui_Handle){ .index = (u32)packed, .generation = (u32)(packed >> 32) };
}

bool mel_gui_alive(Mel_Gui_Handle h);
