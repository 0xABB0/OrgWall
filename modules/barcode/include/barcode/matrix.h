#pragma once

#include <allocator/allocator.h>
#include <core/types.h>

typedef struct mel_barcode_matrix {
    u8* modules;
    i32 width;
    i32 height;
    i32 quiet_zone;
    const Mel_Alloc* allocator;
} mel_barcode_matrix;

bool mel_barcode_matrix_init(mel_barcode_matrix* m, i32 width, i32 height,
                             const Mel_Alloc* allocator);
void mel_barcode_matrix_free(mel_barcode_matrix* m);

bool mel_barcode_matrix_get(const mel_barcode_matrix* m, i32 x, i32 y);
void mel_barcode_matrix_set(mel_barcode_matrix* m, i32 x, i32 y, bool dark);
void mel_barcode_matrix_fill_column(mel_barcode_matrix* m, i32 x, bool dark);
