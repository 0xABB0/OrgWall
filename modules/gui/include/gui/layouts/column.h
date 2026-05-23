#pragma once

#include <gui/layout.h>

typedef struct {
    i32 spacing;
    i32 margin;
    u8  cross_align;
} Mel_Column_Layout_Opt;

Mel_Layout* mel_column_layout_opt(Mel_Column_Layout_Opt opt);
#define mel_column_layout(...) mel_column_layout_opt((Mel_Column_Layout_Opt){__VA_ARGS__})
