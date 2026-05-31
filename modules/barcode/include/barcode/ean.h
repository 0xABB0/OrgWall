#pragma once

#include <barcode/matrix.h>

i32 mel_ean13_checkdigit(const char* digits12);
i32 mel_ean8_checkdigit(const char* digits7);

bool mel_ean13_encode(mel_barcode_matrix* out, const char* digits, i32 height,
                      const Mel_Alloc* allocator);
bool mel_ean8_encode(mel_barcode_matrix* out, const char* digits, i32 height,
                     const Mel_Alloc* allocator);
bool mel_upca_encode(mel_barcode_matrix* out, const char* digits, i32 height,
                     const Mel_Alloc* allocator);
