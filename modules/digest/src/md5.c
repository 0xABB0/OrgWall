#include <digest/md5.h>
#include <string.h>

static inline u32 mel__md5_rotl32(u32 v, int n) { return (v << n) | (v >> (32 - n)); }

static inline u32 mel__md5_read32le(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

static inline void mel__md5_write32le(u8* p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static const u32 mel__md5_k[64] = {
    0xD76AA478U, 0xE8C7B756U, 0x242070DBU, 0xC1BDCEEEU, 0xF57C0FAFU, 0x4787C62AU, 0xA8304613U, 0xFD469501U, 0x698098D8U, 0x8B44F7AFU, 0xFFFF5BB1U, 0x895CD7BEU, 0x6B901122U, 0xFD987193U, 0xA679438EU, 0x49B40821U,
    0xF61E2562U, 0xC040B340U, 0x265E5A51U, 0xE9B6C7AAU, 0xD62F105DU, 0x02441453U, 0xD8A1E681U, 0xE7D3FBC8U, 0x21E1CDE6U, 0xC33707D6U, 0xF4D50D87U, 0x455A14EDU, 0xA9E3E905U, 0xFCEFA3F8U, 0x676F02D9U, 0x8D2A4C8AU,
    0xFFFA3942U, 0x8771F681U, 0x6D9D6122U, 0xFDE5380CU, 0xA4BEEA44U, 0x4BDECFA9U, 0xF6BB4B60U, 0xBEBFBC70U, 0x289B7EC6U, 0xEAA127FAU, 0xD4EF3085U, 0x04881D05U, 0xD9D4D039U, 0xE6DB99E5U, 0x1FA27CF8U, 0xC4AC5665U,
    0xF4292244U, 0x432AFF97U, 0xAB9423A7U, 0xFC93A039U, 0x655B59C3U, 0x8F0CCC92U, 0xFFEFF47DU, 0x85845DD1U, 0x6FA87E4FU, 0xFE2CE6E0U, 0xA3014314U, 0x4E0811A1U, 0xF7537E82U, 0xBD3AF235U, 0x2AD7D2BBU, 0xEB86D391U,
};

static const u8 mel__md5_s[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static void mel__md5_block(u32 h[4], const u8* p)
{
    u32 m[16];
    for (int i = 0; i < 16; i++)
        m[i] = mel__md5_read32le(p + 4 * i);

    u32 a = h[0];
    u32 b = h[1];
    u32 c = h[2];
    u32 d = h[3];

    for (int i = 0; i < 64; i++)
    {
        u32 f, g;
        if (i < 16)
        {
            f = (b & c) | (~b & d);
            g = (u32)i;
        }
        else if (i < 32)
        {
            f = (d & b) | (~d & c);
            g = (5 * (u32)i + 1) & 15;
        }
        else if (i < 48)
        {
            f = b ^ c ^ d;
            g = (3 * (u32)i + 5) & 15;
        }
        else
        {
            f = c ^ (b | ~d);
            g = (7 * (u32)i) & 15;
        }
        u32 t = d;
        d = c;
        c = b;
        b += mel__md5_rotl32(a + f + mel__md5_k[i] + m[g], mel__md5_s[i]);
        a = t;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}

void mel_md5_init(Mel_Md5_State* st)
{
    st->h[0] = 0x67452301U;
    st->h[1] = 0xEFCDAB89U;
    st->h[2] = 0x98BADCFEU;
    st->h[3] = 0x10325476U;
    st->total_len = 0;
}

void mel_md5_update(Mel_Md5_State* st, const void* data, usize len)
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
        mel__md5_block(st->h, st->buffer);
        p += fill;
        len -= fill;
    }

    while (len >= 64)
    {
        mel__md5_block(st->h, p);
        p += 64;
        len -= 64;
    }

    if (len)
        memcpy(st->buffer, p, len);
}

Mel_Md5 mel_md5_final(const Mel_Md5_State* st)
{
    u32 h[4];
    memcpy(h, st->h, sizeof(h));

    u8    block[64];
    usize buffered = (usize)(st->total_len & 63);
    memcpy(block, st->buffer, buffered);
    block[buffered++] = 0x80;

    if (buffered > 56)
    {
        memset(block + buffered, 0, 64 - buffered);
        mel__md5_block(h, block);
        buffered = 0;
    }
    memset(block + buffered, 0, 56 - buffered);

    u64 bits = st->total_len << 3;
    for (int i = 0; i < 8; i++)
        block[56 + i] = (u8)(bits >> (8 * i));
    mel__md5_block(h, block);

    Mel_Md5 out;
    for (int i = 0; i < 4; i++)
        mel__md5_write32le(out.bytes + 4 * i, h[i]);
    return out;
}

Mel_Md5 mel_md5(const void* data, usize len)
{
    Mel_Md5_State st;
    mel_md5_init(&st);
    mel_md5_update(&st, data, len);
    return mel_md5_final(&st);
}
