#include <digest/sm3.h>
#include <string.h>

static inline u32 mel__sm3_rotl32(u32 v, int n) { return (v << n) | (v >> (32 - n)); }

static inline u32 mel__sm3_read32be(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]; }

static inline void mel__sm3_write32be(u8* p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static inline u32 mel__sm3_p0(u32 x) { return x ^ mel__sm3_rotl32(x, 9) ^ mel__sm3_rotl32(x, 17); }
static inline u32 mel__sm3_p1(u32 x) { return x ^ mel__sm3_rotl32(x, 15) ^ mel__sm3_rotl32(x, 23); }

static void mel__sm3_block(u32 h[8], const u8* p)
{
    u32 w[68];
    for (int i = 0; i < 16; i++)
        w[i] = mel__sm3_read32be(p + 4 * i);
    for (int i = 16; i < 68; i++)
        w[i] = mel__sm3_p1(w[i - 16] ^ w[i - 9] ^ mel__sm3_rotl32(w[i - 3], 15)) ^ mel__sm3_rotl32(w[i - 13], 7) ^ w[i - 6];

    u32 a = h[0];
    u32 b = h[1];
    u32 c = h[2];
    u32 d = h[3];
    u32 e = h[4];
    u32 f = h[5];
    u32 g = h[6];
    u32 hh = h[7];

    for (int j = 0; j < 64; j++)
    {
        u32 t = j < 16 ? 0x79CC4519U : 0x7A879D8AU;
        u32 ss1 = mel__sm3_rotl32(mel__sm3_rotl32(a, 12) + e + mel__sm3_rotl32(t, j & 31), 7);
        u32 ss2 = ss1 ^ mel__sm3_rotl32(a, 12);
        u32 ff = j < 16 ? a ^ b ^ c : (a & b) | (a & c) | (b & c);
        u32 gg = j < 16 ? e ^ f ^ g : (e & f) | (~e & g);
        u32 tt1 = ff + d + ss2 + (w[j] ^ w[j + 4]);
        u32 tt2 = gg + hh + ss1 + w[j];
        d = c;
        c = mel__sm3_rotl32(b, 9);
        b = a;
        a = tt1;
        hh = g;
        g = mel__sm3_rotl32(f, 19);
        f = e;
        e = mel__sm3_p0(tt2);
    }

    h[0] ^= a;
    h[1] ^= b;
    h[2] ^= c;
    h[3] ^= d;
    h[4] ^= e;
    h[5] ^= f;
    h[6] ^= g;
    h[7] ^= hh;
}

void mel_sm3_init(Mel_Sm3_State* st)
{
    st->h[0] = 0x7380166FU;
    st->h[1] = 0x4914B2B9U;
    st->h[2] = 0x172442D7U;
    st->h[3] = 0xDA8A0600U;
    st->h[4] = 0xA96F30BCU;
    st->h[5] = 0x163138AAU;
    st->h[6] = 0xE38DEE4DU;
    st->h[7] = 0xB0FB0E4EU;
    st->total_len = 0;
}

void mel_sm3_update(Mel_Sm3_State* st, const void* data, usize len)
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
        mel__sm3_block(st->h, st->buffer);
        p += fill;
        len -= fill;
    }

    while (len >= 64)
    {
        mel__sm3_block(st->h, p);
        p += 64;
        len -= 64;
    }

    if (len)
        memcpy(st->buffer, p, len);
}

Mel_Sm3 mel_sm3_final(const Mel_Sm3_State* st)
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
        mel__sm3_block(h, block);
        buffered = 0;
    }
    memset(block + buffered, 0, 56 - buffered);

    u64 bits = st->total_len << 3;
    for (int i = 0; i < 8; i++)
        block[56 + i] = (u8)(bits >> (8 * (7 - i)));
    mel__sm3_block(h, block);

    Mel_Sm3 out;
    for (int i = 0; i < 8; i++)
        mel__sm3_write32be(out.bytes + 4 * i, h[i]);
    return out;
}

Mel_Sm3 mel_sm3(const void* data, usize len)
{
    Mel_Sm3_State st;
    mel_sm3_init(&st);
    mel_sm3_update(&st, data, len);
    return mel_sm3_final(&st);
}
