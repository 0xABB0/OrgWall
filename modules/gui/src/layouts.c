#include "gui_internal.h"

#include <gui/layouts/column.h>
#include <gui/layouts/row.h>
#include <gui/layouts/stack.h>

static Mel_Layout* linear_new(bool vertical, Mel_Linear_Layout_Opt opt)
{
    Mel_Linear_Layout* l = (Mel_Linear_Layout*)mel_calloc(mel_gui__alloc(), sizeof *l);
    if (!l)
        return NULL;
    mel_linear_layout_init(l, vertical, opt);
    return &l->base;
}

Mel_Layout* mel_column_layout_opt(Mel_Column_Layout_Opt opt) { return linear_new(true, opt); }

Mel_Layout* mel_row_layout_opt(Mel_Row_Layout_Opt opt) { return linear_new(false, opt); }

Mel_Layout* mel_stack_layout_opt(Mel_Stack_Layout_Opt opt)
{
    Mel_Stack_Layout* l = (Mel_Stack_Layout*)mel_calloc(mel_gui__alloc(), sizeof *l);
    if (!l)
        return NULL;
    mel_stack_layout_init(l, opt);
    return &l->base;
}
