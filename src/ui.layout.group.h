#pragma once

#include "ui.layout.h"

typedef struct Mel_GroupLayout {
    Mel_Layout base;
    f32 margin;
    f32 spacing;
    f32 group_spacing;
    f32 group_indent;
} Mel_GroupLayout;

typedef struct Mel_GroupLayout_Opt {
    f32 margin;
    f32 spacing;
    f32 group_spacing;
    f32 group_indent;
} Mel_GroupLayout_Opt;

void mel_group_layout_init_opt(Mel_GroupLayout* layout, Mel_GroupLayout_Opt opt);
#define mel_group_layout_init(layout, ...) mel_group_layout_init_opt((layout), (Mel_GroupLayout_Opt){__VA_ARGS__})
