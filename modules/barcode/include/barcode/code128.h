#pragma once

#include <barcode/matrix.h>

bool mel_code128_encode(mel_barcode_matrix* out, const char* data, i32 height, const Mel_Alloc* allocator);
