#include <digest/ripemd160.h>
#include <string.h>

static inline u32 mel__rmd_rotl32(u32 v, int n) { return (v << n) | (v >> (32 - n)); }

static inline u32 mel__rmd_read32le(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

static inline void mel__rmd_write32le(u8* p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static inline u32 mel__rmd_f(int round, u32 x, u32 y, u32 z)
{
    switch (round)
    {
    case 0:
        return x ^ y ^ z;
    case 1:
        return (x & y) | (~x & z);
    case 2:
        return (x | ~y) ^ z;
    case 3:
        return (x & z) | (y & ~z);
    default:
        return x ^ (y | ~z);
    }
}

static const u32 mel__rmd_kl[5] = { 0x00000000U, 0x5A827999U, 0x6ED9EBA1U, 0x8F1BBCDCU, 0xA953FD4EU };
static const u32 mel__rmd_kr[5] = { 0x50A28BE6U, 0x5C4DD124U, 0x6D703EF3U, 0x7A6D76E9U, 0x00000000U };

static const u8 mel__rmd_rl[80] = {
    0, 1, 2, 3, 4,  5,  6, 7,  8, 9, 10, 11, 12, 13, 14, 15, 7,  4, 13, 1,  10, 6, 15, 3, 12, 0, 9, 5, 2, 14, 11, 8,  3,  10, 14, 4, 9,  15, 8,  1,
    2, 7, 0, 6, 13, 11, 5, 12, 1, 9, 11, 10, 0,  8,  12, 4,  13, 3, 7,  15, 14, 5, 6,  2, 4,  0, 5, 9, 7, 12, 2,  10, 14, 1,  3,  8, 11, 6,  15, 13,
};

static const u8 mel__rmd_rr[80] = {
    5,  14, 7,  0, 9,  2, 11, 4,  13, 6, 15, 8, 1, 10, 3,  12, 6, 11, 3, 7,  0, 13, 5,  10, 14, 15, 8,  12, 4, 9, 1, 2, 15, 5, 1,  3,  7, 14, 6, 9,
    11, 8,  12, 2, 10, 0, 4,  13, 8,  6, 4,  1, 3, 11, 15, 0,  5, 12, 2, 13, 9, 7,  10, 14, 12, 15, 10, 4,  1, 5, 8, 7, 6,  2, 13, 14, 0, 3,  9, 11,
};

static const u8 mel__rmd_sl[80] = {
    11, 14, 15, 12, 5, 8,  7, 9, 11, 13, 14, 15, 6,  7,  9, 8, 7, 6,  8, 13, 11, 9, 7, 15, 7, 12, 15, 9,  11, 7, 13, 12, 11, 13, 6,  7,  14, 9, 13, 15,
    14, 8,  13, 6,  5, 12, 7, 5, 11, 12, 14, 15, 14, 15, 9, 8, 9, 14, 5, 6,  8,  6, 5, 12, 9, 15, 5,  11, 6,  8, 13, 12, 5,  12, 13, 14, 11, 8, 5,  6,
};

static const u8 mel__rmd_sr[80] = {
    8,  9,  9, 11, 13, 15, 15, 5, 7,  7, 8, 11, 14, 14, 12, 6,  9, 13, 15, 7, 12, 8, 9,  11, 7, 7, 12, 7, 6,  15, 13, 11, 9, 7,  15, 11, 8,  6,  6,  14,
    12, 13, 5, 14, 13, 13, 7,  5, 15, 5, 8, 11, 14, 14, 6,  14, 6, 9,  12, 9, 12, 5, 15, 8,  8, 5, 12, 9, 12, 5,  14, 6,  8, 13, 6,  5,  15, 13, 11, 11,
};

static void mel__rmd_block(u32 h[5], const u8* p)
{
    u32 x[16];
    for (int i = 0; i < 16; i++)
        x[i] = mel__rmd_read32le(p + 4 * i);

    u32 al = h[0], bl = h[1], cl = h[2], dl = h[3], el = h[4];
    u32 ar = h[0], br = h[1], cr = h[2], dr = h[3], er = h[4];

    for (int j = 0; j < 80; j++)
    {
        int round = j >> 4;

        u32 t = mel__rmd_rotl32(al + mel__rmd_f(round, bl, cl, dl) + x[mel__rmd_rl[j]] + mel__rmd_kl[round], mel__rmd_sl[j]) + el;
        al = el;
        el = dl;
        dl = mel__rmd_rotl32(cl, 10);
        cl = bl;
        bl = t;

        t = mel__rmd_rotl32(ar + mel__rmd_f(4 - round, br, cr, dr) + x[mel__rmd_rr[j]] + mel__rmd_kr[round], mel__rmd_sr[j]) + er;
        ar = er;
        er = dr;
        dr = mel__rmd_rotl32(cr, 10);
        cr = br;
        br = t;
    }

    u32 t = h[1] + cl + dr;
    h[1] = h[2] + dl + er;
    h[2] = h[3] + el + ar;
    h[3] = h[4] + al + br;
    h[4] = h[0] + bl + cr;
    h[0] = t;
}

void mel_ripemd160_init(Mel_Ripemd160_State* st)
{
    st->h[0] = 0x67452301U;
    st->h[1] = 0xEFCDAB89U;
    st->h[2] = 0x98BADCFEU;
    st->h[3] = 0x10325476U;
    st->h[4] = 0xC3D2E1F0U;
    st->total_len = 0;
}

void mel_ripemd160_update(Mel_Ripemd160_State* st, const void* data, usize len)
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
        mel__rmd_block(st->h, st->buffer);
        p += fill;
        len -= fill;
    }

    while (len >= 64)
    {
        mel__rmd_block(st->h, p);
        p += 64;
        len -= 64;
    }

    if (len)
        memcpy(st->buffer, p, len);
}

Mel_Ripemd160 mel_ripemd160_final(const Mel_Ripemd160_State* st)
{
    u32 h[5];
    memcpy(h, st->h, sizeof(h));

    u8    block[64];
    usize buffered = (usize)(st->total_len & 63);
    memcpy(block, st->buffer, buffered);
    block[buffered++] = 0x80;

    if (buffered > 56)
    {
        memset(block + buffered, 0, 64 - buffered);
        mel__rmd_block(h, block);
        buffered = 0;
    }
    memset(block + buffered, 0, 56 - buffered);

    u64 bits = st->total_len << 3;
    for (int i = 0; i < 8; i++)
        block[56 + i] = (u8)(bits >> (8 * i));
    mel__rmd_block(h, block);

    Mel_Ripemd160 out;
    for (int i = 0; i < 5; i++)
        mel__rmd_write32le(out.bytes + 4 * i, h[i]);
    return out;
}

Mel_Ripemd160 mel_ripemd160(const void* data, usize len)
{
    Mel_Ripemd160_State st;
    mel_ripemd160_init(&st);
    mel_ripemd160_update(&st, data, len);
    return mel_ripemd160_final(&st);
}
