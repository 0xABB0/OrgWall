#include <digest/sha1.h>
#include <string.h>

static inline u32 mel__sha1_rotl32(u32 v, int n) { return (v << n) | (v >> (32 - n)); }

static inline u32 mel__sha1_read32be(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]; }

static inline void mel__sha1_write32be(u8* p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static void mel__sha1_block(u32 h[5], const u8* p)
{
    u32 w[80];
    for (int i = 0; i < 16; i++)
        w[i] = mel__sha1_read32be(p + 4 * i);
    for (int i = 16; i < 80; i++)
        w[i] = mel__sha1_rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    u32 a = h[0];
    u32 b = h[1];
    u32 c = h[2];
    u32 d = h[3];
    u32 e = h[4];

    for (int i = 0; i < 80; i++)
    {
        u32 f, k;
        if (i < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5A827999U;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }
        u32 t = mel__sha1_rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = mel__sha1_rotl32(b, 30);
        b = a;
        a = t;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void mel_sha1_init(Mel_Sha1_State* st)
{
    st->h[0] = 0x67452301U;
    st->h[1] = 0xEFCDAB89U;
    st->h[2] = 0x98BADCFEU;
    st->h[3] = 0x10325476U;
    st->h[4] = 0xC3D2E1F0U;
    st->total_len = 0;
}

void mel_sha1_update(Mel_Sha1_State* st, const void* data, usize len)
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
        mel__sha1_block(st->h, st->buffer);
        p += fill;
        len -= fill;
    }

    while (len >= 64)
    {
        mel__sha1_block(st->h, p);
        p += 64;
        len -= 64;
    }

    if (len)
        memcpy(st->buffer, p, len);
}

Mel_Sha1 mel_sha1_final(const Mel_Sha1_State* st)
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
        mel__sha1_block(h, block);
        buffered = 0;
    }
    memset(block + buffered, 0, 56 - buffered);

    u64 bits = st->total_len << 3;
    for (int i = 0; i < 8; i++)
        block[56 + i] = (u8)(bits >> (8 * (7 - i)));
    mel__sha1_block(h, block);

    Mel_Sha1 out;
    for (int i = 0; i < 5; i++)
        mel__sha1_write32be(out.bytes + 4 * i, h[i]);
    return out;
}

Mel_Sha1 mel_sha1(const void* data, usize len)
{
    Mel_Sha1_State st;
    mel_sha1_init(&st);
    mel_sha1_update(&st, data, len);
    return mel_sha1_final(&st);
}
