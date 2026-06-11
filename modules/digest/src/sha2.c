#include <digest/sha2.h>
#include <string.h>

static inline u32 mel__sha2_rotr32(u32 v, int n) { return (v >> n) | (v << (32 - n)); }
static inline u64 mel__sha2_rotr64(u64 v, int n) { return (v >> n) | (v << (64 - n)); }

static inline u32 mel__sha2_read32be(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]; }

static inline u64 mel__sha2_read64be(const u8* p) { return ((u64)p[0] << 56) | ((u64)p[1] << 48) | ((u64)p[2] << 40) | ((u64)p[3] << 32) | ((u64)p[4] << 24) | ((u64)p[5] << 16) | ((u64)p[6] << 8) | (u64)p[7]; }

static inline void mel__sha2_write32be(u8* p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static inline void mel__sha2_write64be(u8* p, u64 v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (u8)(v >> (8 * (7 - i)));
}

static const u32 mel__sha256_k[64] = {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U, 0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU, 0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U, 0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U, 0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U, 0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U, 0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U, 0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
};

static const u64 mel__sha512_k[80] = {
    0x428A2F98D728AE22ULL, 0x7137449123EF65CDULL, 0xB5C0FBCFEC4D3B2FULL, 0xE9B5DBA58189DBBCULL, 0x3956C25BF348B538ULL, 0x59F111F1B605D019ULL, 0x923F82A4AF194F9BULL, 0xAB1C5ED5DA6D8118ULL, 0xD807AA98A3030242ULL, 0x12835B0145706FBEULL,
    0x243185BE4EE4B28CULL, 0x550C7DC3D5FFB4E2ULL, 0x72BE5D74F27B896FULL, 0x80DEB1FE3B1696B1ULL, 0x9BDC06A725C71235ULL, 0xC19BF174CF692694ULL, 0xE49B69C19EF14AD2ULL, 0xEFBE4786384F25E3ULL, 0x0FC19DC68B8CD5B5ULL, 0x240CA1CC77AC9C65ULL,
    0x2DE92C6F592B0275ULL, 0x4A7484AA6EA6E483ULL, 0x5CB0A9DCBD41FBD4ULL, 0x76F988DA831153B5ULL, 0x983E5152EE66DFABULL, 0xA831C66D2DB43210ULL, 0xB00327C898FB213FULL, 0xBF597FC7BEEF0EE4ULL, 0xC6E00BF33DA88FC2ULL, 0xD5A79147930AA725ULL,
    0x06CA6351E003826FULL, 0x142929670A0E6E70ULL, 0x27B70A8546D22FFCULL, 0x2E1B21385C26C926ULL, 0x4D2C6DFC5AC42AEDULL, 0x53380D139D95B3DFULL, 0x650A73548BAF63DEULL, 0x766A0ABB3C77B2A8ULL, 0x81C2C92E47EDAEE6ULL, 0x92722C851482353BULL,
    0xA2BFE8A14CF10364ULL, 0xA81A664BBC423001ULL, 0xC24B8B70D0F89791ULL, 0xC76C51A30654BE30ULL, 0xD192E819D6EF5218ULL, 0xD69906245565A910ULL, 0xF40E35855771202AULL, 0x106AA07032BBD1B8ULL, 0x19A4C116B8D2D0C8ULL, 0x1E376C085141AB53ULL,
    0x2748774CDF8EEB99ULL, 0x34B0BCB5E19B48A8ULL, 0x391C0CB3C5C95A63ULL, 0x4ED8AA4AE3418ACBULL, 0x5B9CCA4F7763E373ULL, 0x682E6FF3D6B2B8A3ULL, 0x748F82EE5DEFB2FCULL, 0x78A5636F43172F60ULL, 0x84C87814A1F0AB72ULL, 0x8CC702081A6439ECULL,
    0x90BEFFFA23631E28ULL, 0xA4506CEBDE82BDE9ULL, 0xBEF9A3F7B2C67915ULL, 0xC67178F2E372532BULL, 0xCA273ECEEA26619CULL, 0xD186B8C721C0C207ULL, 0xEADA7DD6CDE0EB1EULL, 0xF57D4F7FEE6ED178ULL, 0x06F067AA72176FBAULL, 0x0A637DC5A2C898A6ULL,
    0x113F9804BEF90DAEULL, 0x1B710B35131C471BULL, 0x28DB77F523047D84ULL, 0x32CAAB7B40C72493ULL, 0x3C9EBE0A15C9BEBCULL, 0x431D67C49C100D4CULL, 0x4CC5D4BECB3E42B6ULL, 0x597F299CFC657E2AULL, 0x5FCB6FAB3AD6FAECULL, 0x6C44198C4A475817ULL,
};

static void mel__sha256_block(u32 h[8], const u8* p)
{
    u32 w[64];
    for (int i = 0; i < 16; i++)
        w[i] = mel__sha2_read32be(p + 4 * i);
    for (int i = 16; i < 64; i++)
    {
        u32 s0 = mel__sha2_rotr32(w[i - 15], 7) ^ mel__sha2_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = mel__sha2_rotr32(w[i - 2], 17) ^ mel__sha2_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    u32 a = h[0];
    u32 b = h[1];
    u32 c = h[2];
    u32 d = h[3];
    u32 e = h[4];
    u32 f = h[5];
    u32 g = h[6];
    u32 hh = h[7];

    for (int i = 0; i < 64; i++)
    {
        u32 s1 = mel__sha2_rotr32(e, 6) ^ mel__sha2_rotr32(e, 11) ^ mel__sha2_rotr32(e, 25);
        u32 ch = (e & f) ^ (~e & g);
        u32 t1 = hh + s1 + ch + mel__sha256_k[i] + w[i];
        u32 s0 = mel__sha2_rotr32(a, 2) ^ mel__sha2_rotr32(a, 13) ^ mel__sha2_rotr32(a, 22);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = s0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

static void mel__sha512_block(u64 h[8], const u8* p)
{
    u64 w[80];
    for (int i = 0; i < 16; i++)
        w[i] = mel__sha2_read64be(p + 8 * i);
    for (int i = 16; i < 80; i++)
    {
        u64 s0 = mel__sha2_rotr64(w[i - 15], 1) ^ mel__sha2_rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        u64 s1 = mel__sha2_rotr64(w[i - 2], 19) ^ mel__sha2_rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    u64 a = h[0];
    u64 b = h[1];
    u64 c = h[2];
    u64 d = h[3];
    u64 e = h[4];
    u64 f = h[5];
    u64 g = h[6];
    u64 hh = h[7];

    for (int i = 0; i < 80; i++)
    {
        u64 s1 = mel__sha2_rotr64(e, 14) ^ mel__sha2_rotr64(e, 18) ^ mel__sha2_rotr64(e, 41);
        u64 ch = (e & f) ^ (~e & g);
        u64 t1 = hh + s1 + ch + mel__sha512_k[i] + w[i];
        u64 s0 = mel__sha2_rotr64(a, 28) ^ mel__sha2_rotr64(a, 34) ^ mel__sha2_rotr64(a, 39);
        u64 maj = (a & b) ^ (a & c) ^ (b & c);
        u64 t2 = s0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

void mel_sha256_init(Mel_Sha256_State* st)
{
    static const u32 iv[8] = { 0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU, 0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U };
    memcpy(st->h, iv, sizeof(iv));
    st->total_len = 0;
}

void mel_sha224_init(Mel_Sha256_State* st)
{
    static const u32 iv[8] = { 0xC1059ED8U, 0x367CD507U, 0x3070DD17U, 0xF70E5939U, 0xFFC00B31U, 0x68581511U, 0x64F98FA7U, 0xBEFA4FA4U };
    memcpy(st->h, iv, sizeof(iv));
    st->total_len = 0;
}

void mel_sha256_update(Mel_Sha256_State* st, const void* data, usize len)
{
    const u8* p = (const u8*)data;
    usize     buffered = (usize)(st->total_len & 63);
    st->total_len += len;

    if (buffered)
    {
        usize fill = 64 - buffered;
        if (len < fill)
        {
            memcpy(st->buffer + buffered, p, len);
            return;
        }
        memcpy(st->buffer + buffered, p, fill);
        mel__sha256_block(st->h, st->buffer);
        p += fill;
        len -= fill;
    }

    while (len >= 64)
    {
        mel__sha256_block(st->h, p);
        p += 64;
        len -= 64;
    }

    if (len)
        memcpy(st->buffer, p, len);
}

static void mel__sha256_digest(const Mel_Sha256_State* st, u8* out, usize out_words)
{
    u32 h[8];
    memcpy(h, st->h, sizeof(h));

    u8    block[64];
    usize buffered = (usize)(st->total_len & 63);
    memcpy(block, st->buffer, buffered);
    block[buffered++] = 0x80;

    if (buffered > 56)
    {
        memset(block + buffered, 0, 64 - buffered);
        mel__sha256_block(h, block);
        buffered = 0;
    }
    memset(block + buffered, 0, 56 - buffered);
    mel__sha2_write64be(block + 56, st->total_len << 3);
    mel__sha256_block(h, block);

    for (usize i = 0; i < out_words; i++)
        mel__sha2_write32be(out + 4 * i, h[i]);
}

Mel_Sha256 mel_sha256_final(const Mel_Sha256_State* st)
{
    Mel_Sha256 out;
    mel__sha256_digest(st, out.bytes, 8);
    return out;
}

Mel_Sha224 mel_sha224_final(const Mel_Sha256_State* st)
{
    Mel_Sha224 out;
    mel__sha256_digest(st, out.bytes, 7);
    return out;
}

void mel_sha512_init(Mel_Sha512_State* st)
{
    static const u64 iv[8] = {
        0x6A09E667F3BCC908ULL, 0xBB67AE8584CAA73BULL, 0x3C6EF372FE94F82BULL, 0xA54FF53A5F1D36F1ULL, 0x510E527FADE682D1ULL, 0x9B05688C2B3E6C1FULL, 0x1F83D9ABFB41BD6BULL, 0x5BE0CD19137E2179ULL,
    };
    memcpy(st->h, iv, sizeof(iv));
    st->total_len = 0;
}

void mel_sha384_init(Mel_Sha512_State* st)
{
    static const u64 iv[8] = {
        0xCBBB9D5DC1059ED8ULL, 0x629A292A367CD507ULL, 0x9159015A3070DD17ULL, 0x152FECD8F70E5939ULL, 0x67332667FFC00B31ULL, 0x8EB44A8768581511ULL, 0xDB0C2E0D64F98FA7ULL, 0x47B5481DBEFA4FA4ULL,
    };
    memcpy(st->h, iv, sizeof(iv));
    st->total_len = 0;
}

void mel_sha512_224_init(Mel_Sha512_State* st)
{
    static const u64 iv[8] = {
        0x8C3D37C819544DA2ULL, 0x73E1996689DCD4D6ULL, 0x1DFAB7AE32FF9C82ULL, 0x679DD514582F9FCFULL, 0x0F6D2B697BD44DA8ULL, 0x77E36F7304C48942ULL, 0x3F9D85A86A1D36C8ULL, 0x1112E6AD91D692A1ULL,
    };
    memcpy(st->h, iv, sizeof(iv));
    st->total_len = 0;
}

void mel_sha512_256_init(Mel_Sha512_State* st)
{
    static const u64 iv[8] = {
        0x22312194FC2BF72CULL, 0x9F555FA3C84C64C2ULL, 0x2393B86B6F53B151ULL, 0x963877195940EABDULL, 0x96283EE2A88EFFE3ULL, 0xBE5E1E2553863992ULL, 0x2B0199FC2C85B8AAULL, 0x0EB72DDC81C52CA2ULL,
    };
    memcpy(st->h, iv, sizeof(iv));
    st->total_len = 0;
}

void mel_sha512_update(Mel_Sha512_State* st, const void* data, usize len)
{
    const u8* p = (const u8*)data;
    usize     buffered = (usize)(st->total_len & 127);
    st->total_len += len;

    if (buffered)
    {
        usize fill = 128 - buffered;
        if (len < fill)
        {
            memcpy(st->buffer + buffered, p, len);
            return;
        }
        memcpy(st->buffer + buffered, p, fill);
        mel__sha512_block(st->h, st->buffer);
        p += fill;
        len -= fill;
    }

    while (len >= 128)
    {
        mel__sha512_block(st->h, p);
        p += 128;
        len -= 128;
    }

    if (len)
        memcpy(st->buffer, p, len);
}

static void mel__sha512_digest(const Mel_Sha512_State* st, u8* out, usize out_bytes)
{
    u64 h[8];
    memcpy(h, st->h, sizeof(h));

    u8    block[128];
    usize buffered = (usize)(st->total_len & 127);
    memcpy(block, st->buffer, buffered);
    block[buffered++] = 0x80;

    if (buffered > 112)
    {
        memset(block + buffered, 0, 128 - buffered);
        mel__sha512_block(h, block);
        buffered = 0;
    }
    memset(block + buffered, 0, 112 - buffered);
    mel__sha2_write64be(block + 112, st->total_len >> 61);
    mel__sha2_write64be(block + 120, st->total_len << 3);
    mel__sha512_block(h, block);

    u8 full[64];
    for (int i = 0; i < 8; i++)
        mel__sha2_write64be(full + 8 * i, h[i]);
    memcpy(out, full, out_bytes);
}

Mel_Sha512 mel_sha512_final(const Mel_Sha512_State* st)
{
    Mel_Sha512 out;
    mel__sha512_digest(st, out.bytes, 64);
    return out;
}

Mel_Sha384 mel_sha384_final(const Mel_Sha512_State* st)
{
    Mel_Sha384 out;
    mel__sha512_digest(st, out.bytes, 48);
    return out;
}

Mel_Sha512_224 mel_sha512_224_final(const Mel_Sha512_State* st)
{
    Mel_Sha512_224 out;
    mel__sha512_digest(st, out.bytes, 28);
    return out;
}

Mel_Sha512_256 mel_sha512_256_final(const Mel_Sha512_State* st)
{
    Mel_Sha512_256 out;
    mel__sha512_digest(st, out.bytes, 32);
    return out;
}

Mel_Sha224 mel_sha224(const void* data, usize len)
{
    Mel_Sha256_State st;
    mel_sha224_init(&st);
    mel_sha256_update(&st, data, len);
    return mel_sha224_final(&st);
}

Mel_Sha256 mel_sha256(const void* data, usize len)
{
    Mel_Sha256_State st;
    mel_sha256_init(&st);
    mel_sha256_update(&st, data, len);
    return mel_sha256_final(&st);
}

Mel_Sha384 mel_sha384(const void* data, usize len)
{
    Mel_Sha512_State st;
    mel_sha384_init(&st);
    mel_sha512_update(&st, data, len);
    return mel_sha384_final(&st);
}

Mel_Sha512 mel_sha512(const void* data, usize len)
{
    Mel_Sha512_State st;
    mel_sha512_init(&st);
    mel_sha512_update(&st, data, len);
    return mel_sha512_final(&st);
}

Mel_Sha512_224 mel_sha512_224(const void* data, usize len)
{
    Mel_Sha512_State st;
    mel_sha512_224_init(&st);
    mel_sha512_update(&st, data, len);
    return mel_sha512_224_final(&st);
}

Mel_Sha512_256 mel_sha512_256(const void* data, usize len)
{
    Mel_Sha512_State st;
    mel_sha512_256_init(&st);
    mel_sha512_update(&st, data, len);
    return mel_sha512_256_final(&st);
}
