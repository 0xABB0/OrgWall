#pragma once

#include <layout/layout.h>

/* One axis-parametric kind serves column (vertical) and row (horizontal).
 * The struct is public data on purpose: a host that recognises the class
 * reads these fields to lower the layout to a native engine. */
typedef struct
{
    Mel_Layout base;
    bool       vertical;
    i32        spacing;
    i32        margin;
    u8         cross_align;
} Mel_Linear_Layout;

typedef struct
{
    i32 spacing;
    i32 margin;
    u8  cross_align;
} Mel_Linear_Layout_Opt;

const Mel_Layout_Class* mel_linear_layout_class(void);

void mel_linear_layout_init(Mel_Linear_Layout* layout, bool vertical, Mel_Linear_Layout_Opt opt);
