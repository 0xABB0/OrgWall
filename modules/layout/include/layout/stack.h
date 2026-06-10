#pragma once

#include <layout/layout.h>

/* Overlay: every child fills the container minus the container margin and the
 * child's own margins. Per-child alignment within the overlay is deferred. */
typedef struct
{
    Mel_Layout base;
    i32        margin;
} Mel_Stack_Layout;

typedef struct
{
    i32 margin;
} Mel_Stack_Layout_Opt;

const Mel_Layout_Class* mel_stack_layout_class(void);

void mel_stack_layout_init(Mel_Stack_Layout* layout, Mel_Stack_Layout_Opt opt);
