#pragma once

#include "ui.layout.h"

typedef struct Mel_GridLayout {
    Mel_Layout base;
    i32 orientation;
    i32 resolution;
    i32 alignment;
    f32 margin;
    f32 spacing;
} Mel_GridLayout;

typedef struct Mel_GridLayout_Opt {
    i32 orientation;
    i32 resolution;
    i32 alignment;
    f32 margin;
    f32 spacing;
} Mel_GridLayout_Opt;

void mel_grid_layout_init_opt(Mel_GridLayout* layout, Mel_GridLayout_Opt opt);
#define mel_grid_layout_init(layout, ...) mel_grid_layout_init_opt((layout), (Mel_GridLayout_Opt){__VA_ARGS__})
