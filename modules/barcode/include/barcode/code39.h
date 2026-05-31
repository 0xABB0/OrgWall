#pragma once

#include <barcode/matrix.h>

typedef struct mel_code39_opt {
    bool checksum;
} mel_code39_opt;

bool mel_code39_encode(mel_barcode_matrix* out, const char* data, i32 height,
                       mel_code39_opt opt, const Mel_Alloc* allocator);
