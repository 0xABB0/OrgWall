#pragma once

#include <allocator/allocator.h>
#include <core/types.h>
#include <image/image.h>

#include <barcode/qr.h>

bool mel_qr_detect_image(const mel_image_gray* gray, mel_barcode_matrix* out_grid, const Mel_Alloc* allocator);
bool mel_qr_decode_image(const mel_image_gray* gray, mel_qr_decoded* out, const Mel_Alloc* allocator);
bool mel_qr_decode_image_gf(const mel_image_gray* gray, mel_qr_decoded* out, mel_gf* gf, const Mel_Alloc* allocator);
