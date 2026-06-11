#include <digest/hmac.h>
#include <digest/sha2.h>
#include <string.h>

bool mel_digest_eq(const u8* a, const u8* b, usize len)
{
    u8 acc = 0;
    for (usize i = 0; i < len; i++)
        acc |= a[i] ^ b[i];
    return acc == 0;
}

void mel_hmac_sha256(const u8* key, usize key_len, const u8* msg, usize msg_len, u8 out[32])
{
    u8 k[64];
    if (key_len > 64)
    {
        Mel_Sha256 hk = mel_sha256(key, key_len);
        memcpy(k, hk.bytes, 32);
        memset(k + 32, 0, 32);
    }
    else
    {
        memcpy(k, key, key_len);
        memset(k + key_len, 0, 64 - key_len);
    }

    u8 ipad[64], opad[64];
    for (int i = 0; i < 64; i++)
    {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    Mel_Sha256_State st;
    mel_sha256_init(&st);
    mel_sha256_update(&st, ipad, 64);
    mel_sha256_update(&st, msg, msg_len);
    Mel_Sha256 inner = mel_sha256_final(&st);

    mel_sha256_init(&st);
    mel_sha256_update(&st, opad, 64);
    mel_sha256_update(&st, inner.bytes, 32);
    Mel_Sha256 result = mel_sha256_final(&st);

    memcpy(out, result.bytes, 32);
}

void mel_hmac_sha512(const u8* key, usize key_len, const u8* msg, usize msg_len, u8 out[64])
{
    u8 k[128];
    if (key_len > 128)
    {
        Mel_Sha512 hk = mel_sha512(key, key_len);
        memcpy(k, hk.bytes, 64);
        memset(k + 64, 0, 64);
    }
    else
    {
        memcpy(k, key, key_len);
        memset(k + key_len, 0, 128 - key_len);
    }

    u8 ipad[128], opad[128];
    for (int i = 0; i < 128; i++)
    {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    Mel_Sha512_State st;
    mel_sha512_init(&st);
    mel_sha512_update(&st, ipad, 128);
    mel_sha512_update(&st, msg, msg_len);
    Mel_Sha512 inner = mel_sha512_final(&st);

    mel_sha512_init(&st);
    mel_sha512_update(&st, opad, 128);
    mel_sha512_update(&st, inner.bytes, 64);
    Mel_Sha512 result = mel_sha512_final(&st);

    memcpy(out, result.bytes, 64);
}
