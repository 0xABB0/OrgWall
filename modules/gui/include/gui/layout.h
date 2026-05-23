#pragma once

#include <core/types.h>

#include <gui/handle.h>

typedef enum
{
    MEL_ALIGN_DEFAULT = 0,
    MEL_ALIGN_START = 1,
    MEL_ALIGN_CENTER = 2,
    MEL_ALIGN_END = 3,
    MEL_ALIGN_STRETCH = 4,
} Mel_Align;

typedef struct
{
    i32 preferred_w;
    i32 preferred_h;
    i32 fixed_w;
    i32 fixed_h;
    i32 weight;
    u8  cross_align;
    i32 margin_l;
    i32 margin_t;
    i32 margin_r;
    i32 margin_b;
} Mel_Layoutable;

typedef struct Mel_Layout Mel_Layout;

typedef struct
{
    void (*measure)(Mel_Layout*, Mel_Gui_Handle container, i32 avail_w, i32 avail_h, i32* out_w, i32* out_h);
    void (*arrange)(Mel_Layout*, Mel_Gui_Handle container);
} Mel_Layout_Vtable;

struct Mel_Layout
{
    const Mel_Layout_Vtable* vtable;
};

void mel_gui_set_layout(Mel_Gui_Handle parent, Mel_Layout* layout);
void mel_gui_relayout(Mel_Gui_Handle handle);
