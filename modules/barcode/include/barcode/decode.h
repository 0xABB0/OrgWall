#pragma once

#include <allocator/allocator.h>
#include <core/types.h>
#include <image/image.h>

#include <barcode/galois.h>

typedef struct mel_barcode_decode_result
{
    const char* symbology;
    char*       text;
    i32         text_len;
    i32         x_start;
    i32         x_end;
    i32         y;
    bool        found;
} mel_barcode_decode_result;

typedef struct mel_barcode_decoder
{
    const Mel_Alloc* alloc;
    i32*             widths;
    i32*             values;
    i32              widths_cap;
    i32              values_cap;
    mel_gf           gf;
    bool             gf_ready;
} mel_barcode_decoder;

bool mel_barcode_decoder_init(mel_barcode_decoder* dec, i32 max_width, const Mel_Alloc* allocator);
void mel_barcode_decoder_free(mel_barcode_decoder* dec);
bool mel_barcode_decoder_decode(mel_barcode_decoder* dec, const mel_image_gray* gray, mel_barcode_decode_result* out);

bool mel_barcode_decode(const mel_image_gray* gray, mel_barcode_decode_result* out, const Mel_Alloc* allocator);
void mel_barcode_decode_result_free(mel_barcode_decode_result* r, const Mel_Alloc* allocator);
