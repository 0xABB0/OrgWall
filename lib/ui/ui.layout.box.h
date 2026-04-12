#pragma once

#include "ui.layout.h"

typedef struct Mel_BoxLayout {
    Mel_Layout base;
    i32 orientation;
    i32 alignment;
    f32 margin;
    f32 spacing;
} Mel_BoxLayout;

typedef struct Mel_BoxLayout_Opt {
    i32 orientation;
    i32 alignment;
    f32 margin;
    f32 spacing;
} Mel_BoxLayout_Opt;

void mel_box_layout_init_opt(Mel_BoxLayout* layout, Mel_BoxLayout_Opt opt);
#define mel_box_layout_init(layout, ...) mel_box_layout_init_opt((layout), (Mel_BoxLayout_Opt){__VA_ARGS__})
