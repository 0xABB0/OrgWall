#pragma once

#include <barcode/galois.h>

bool mel_rs_generate(const mel_gf* f, u16 alpha, u32 first_root, const u16* data, usize n_data, usize n_ecc, u16* ecc_out);

bool mel_rs_decode(const mel_gf* f, u16 alpha, u32 first_root, u16* codeword, usize n_total, usize n_ecc, const u16* erasures, usize n_erasures, usize* out_corrected, const Mel_Alloc* a);
