#pragma once

#include <core/types.h>

bool mel_digest_eq(const u8* a, const u8* b, usize len);

void mel_hmac_sha256(const u8* key, usize key_len, const u8* msg, usize msg_len, u8 out[32]);
void mel_hmac_sha512(const u8* key, usize key_len, const u8* msg, usize msg_len, u8 out[64]);
