#pragma once

#include <barcode/matrix.h>

typedef struct mel_itf_opt {
    bool checksum;
    bool pad_odd;
} mel_itf_opt;

bool mel_itf_encode(mel_barcode_matrix* out, const char* digits, i32 height,
                    mel_itf_opt opt, const Mel_Alloc* allocator);
