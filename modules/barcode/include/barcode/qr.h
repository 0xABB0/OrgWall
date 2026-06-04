#pragma once

#include <barcode/galois.h>
#include <barcode/matrix.h>

typedef struct mel_qr_ecc
{
    u8 rank;
} mel_qr_ecc;

mel_qr_ecc mel_qr_ecc_l(void);
mel_qr_ecc mel_qr_ecc_m(void);
mel_qr_ecc mel_qr_ecc_q(void);
mel_qr_ecc mel_qr_ecc_h(void);

bool mel_qr_codewords(const char* data, mel_qr_ecc ecc, i32 version_hint, const Mel_Alloc* allocator, u8** out_codewords, usize* out_count, i32* out_version);

typedef struct mel_qr_opt
{
    mel_qr_ecc ecc;
    i32        version;
    i32        mask;
} mel_qr_opt;

bool mel_qr_encode(mel_barcode_matrix* out, const char* data, mel_qr_opt opt, const Mel_Alloc* allocator);

typedef struct mel_qr_decoded
{
    char*      text;
    usize      len;
    i32        version;
    mel_qr_ecc ecc;
    i32        mask;
} mel_qr_decoded;

bool mel_qr_decode(const mel_barcode_matrix* in, mel_qr_decoded* out, const Mel_Alloc* allocator);
bool mel_qr_decode_gf(const mel_barcode_matrix* in, mel_qr_decoded* out, mel_gf* gf, const Mel_Alloc* allocator);
void mel_qr_decoded_free(mel_qr_decoded* d, const Mel_Alloc* allocator);
