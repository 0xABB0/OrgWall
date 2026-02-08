#pragma once

#include "ui.layout.h"

typedef struct Mel_Alloc Mel_Alloc;

typedef struct Mel_GridAnchor {
    i32 col;
    i32 row;
    i32 col_span;
    i32 row_span;
    i32 align_x;
    i32 align_y;
} Mel_GridAnchor;

typedef struct Mel_AdvGridLayout {
    Mel_Layout base;
    const Mel_Alloc* allocator;
    i32 cols;
    i32 rows;
    f32* col_stretch;
    f32* row_stretch;
    f32 margin;
    f32 spacing;
    Mel_Layoutable** anchor_keys;
    Mel_GridAnchor*  anchor_values;
    i32 anchor_count;
    i32 anchor_capacity;
} Mel_AdvGridLayout;

typedef struct Mel_AdvGridLayout_Opt {
    const Mel_Alloc* allocator;
    i32 cols;
    i32 rows;
    f32* col_stretch;
    f32* row_stretch;
    f32 margin;
    f32 spacing;
} Mel_AdvGridLayout_Opt;

void mel_advgrid_layout_init_opt(Mel_AdvGridLayout* layout, Mel_AdvGridLayout_Opt opt);
#define mel_advgrid_layout_init(layout, ...) mel_advgrid_layout_init_opt((layout), (Mel_AdvGridLayout_Opt){__VA_ARGS__})

void mel_advgrid_layout_set_anchor(Mel_AdvGridLayout* layout, Mel_Layoutable* item, Mel_GridAnchor anchor);
void mel_advgrid_layout_destroy(Mel_AdvGridLayout* layout);
