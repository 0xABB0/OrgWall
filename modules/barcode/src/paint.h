#pragma once

#include <barcode/matrix.h>

typedef struct mel__painter
{
    mel_barcode_matrix* m;
    i32                 x;
} mel__painter;

static void mel__paint_run(mel__painter* p, i32 width, bool dark)
{
    for (i32 i = 0; i < width; ++i)
    {
        if (dark)
        {
            mel_barcode_matrix_fill_column(p->m, p->x, true);
        }
        p->x += 1;
    }
}
