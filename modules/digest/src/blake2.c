#include <digest/blake2.h>
#include <string.h>

static inline u64 mel__b2_rotr64(u64 v, int n) { return (v >> n) | (v << (64 - n)); }
static inline u32 mel__b2_rotr32(u32 v, int n) { return (v >> n) | (v << (32 - n)); }

static inline u64 mel__b2_read64le(const u8* p)
{
    u64 v;
    memcpy(&v, p, sizeof(v));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap64(v);
#endif
    return v;
}

static inline u32 mel__b2_read32le(const u8* p)
{
    u32 v;
    memcpy(&v, p, sizeof(v));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap32(v);
#endif
    return v;
}

static const u64 mel__blake2b_iv[8] = {
    0x6A09E667F3BCC908ULL, 0xBB67AE8584CAA73BULL, 0x3C6EF372FE94F82BULL, 0xA54FF53A5F1D36F1ULL, 0x510E527FADE682D1ULL, 0x9B05688C2B3E6C1FULL, 0x1F83D9ABFB41BD6BULL, 0x5BE0CD19137E2179ULL,
};

static const u32 mel__blake2s_iv[8] = {
    0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU, 0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U,
};

static const u8 mel__b2_sigma[10][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 }, { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 }, { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 }, { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 }, { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 }, { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 }, { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
};

static inline void mel__blake2b_g(u64 v[16], int a, int b, int c, int d, u64 x, u64 y)
{
    v[a] += v[b] + x;
    v[d] = mel__b2_rotr64(v[d] ^ v[a], 32);
    v[c] += v[d];
    v[b] = mel__b2_rotr64(v[b] ^ v[c], 24);
    v[a] += v[b] + y;
    v[d] = mel__b2_rotr64(v[d] ^ v[a], 16);
    v[c] += v[d];
    v[b] = mel__b2_rotr64(v[b] ^ v[c], 63);
}

static inline void mel__blake2s_g(u32 v[16], int a, int b, int c, int d, u32 x, u32 y)
{
    v[a] += v[b] + x;
    v[d] = mel__b2_rotr32(v[d] ^ v[a], 16);
    v[c] += v[d];
    v[b] = mel__b2_rotr32(v[b] ^ v[c], 12);
    v[a] += v[b] + y;
    v[d] = mel__b2_rotr32(v[d] ^ v[a], 8);
    v[c] += v[d];
    v[b] = mel__b2_rotr32(v[b] ^ v[c], 7);
}

static void mel__blake2b_compress(u64 h[8], const u8* block, u64 t0, u64 t1, u64 f0)
{
    u64 m[16];
    for (int i = 0; i < 16; i++)
        m[i] = mel__b2_read64le(block + 8 * i);

    u64 v[16];
    memcpy(v, h, 8 * sizeof(u64));
    memcpy(v + 8, mel__blake2b_iv, 8 * sizeof(u64));
    v[12] ^= t0;
    v[13] ^= t1;
    v[14] ^= f0;

    for (int r = 0; r < 12; r++)
    {
        const u8* s = mel__b2_sigma[r % 10];
        mel__blake2b_g(v, 0, 4, 8, 12, m[s[0]], m[s[1]]);
        mel__blake2b_g(v, 1, 5, 9, 13, m[s[2]], m[s[3]]);
        mel__blake2b_g(v, 2, 6, 10, 14, m[s[4]], m[s[5]]);
        mel__blake2b_g(v, 3, 7, 11, 15, m[s[6]], m[s[7]]);
        mel__blake2b_g(v, 0, 5, 10, 15, m[s[8]], m[s[9]]);
        mel__blake2b_g(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
        mel__blake2b_g(v, 2, 7, 8, 13, m[s[12]], m[s[13]]);
        mel__blake2b_g(v, 3, 4, 9, 14, m[s[14]], m[s[15]]);
    }

    for (int i = 0; i < 8; i++)
        h[i] ^= v[i] ^ v[i + 8];
}

static void mel__blake2s_compress(u32 h[8], const u8* block, u32 t0, u32 t1, u32 f0)
{
    u32 m[16];
    for (int i = 0; i < 16; i++)
        m[i] = mel__b2_read32le(block + 4 * i);

    u32 v[16];
    memcpy(v, h, 8 * sizeof(u32));
    memcpy(v + 8, mel__blake2s_iv, 8 * sizeof(u32));
    v[12] ^= t0;
    v[13] ^= t1;
    v[14] ^= f0;

    for (int r = 0; r < 10; r++)
    {
        const u8* s = mel__b2_sigma[r];
        mel__blake2s_g(v, 0, 4, 8, 12, m[s[0]], m[s[1]]);
        mel__blake2s_g(v, 1, 5, 9, 13, m[s[2]], m[s[3]]);
        mel__blake2s_g(v, 2, 6, 10, 14, m[s[4]], m[s[5]]);
        mel__blake2s_g(v, 3, 7, 11, 15, m[s[6]], m[s[7]]);
        mel__blake2s_g(v, 0, 5, 10, 15, m[s[8]], m[s[9]]);
        mel__blake2s_g(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
        mel__blake2s_g(v, 2, 7, 8, 13, m[s[12]], m[s[13]]);
        mel__blake2s_g(v, 3, 4, 9, 14, m[s[14]], m[s[15]]);
    }

    for (int i = 0; i < 8; i++)
        h[i] ^= v[i] ^ v[i + 8];
}

void mel_blake2b_init(Mel_Blake2b_State* st, usize out_len, const void* key, usize key_len)
{
    assert(out_len >= 1 && out_len <= 64);
    assert(key_len <= 64);

    memcpy(st->h, mel__blake2b_iv, sizeof(st->h));
    st->h[0] ^= 0x01010000ULL ^ ((u64)key_len << 8) ^ (u64)out_len;
    st->t[0] = 0;
    st->t[1] = 0;
    st->buffered = 0;
    st->out_len = out_len;

    if (key_len)
    {
        memset(st->buffer, 0, sizeof(st->buffer));
        memcpy(st->buffer, key, key_len);
        st->buffered = 128;
    }
}

void mel_blake2b_update(Mel_Blake2b_State* st, const void* data, usize len)
{
    if (!len)
        return;

    const u8* p = (const u8*)data;
    if (st->buffered + len > 128)
    {
        usize fill = 128 - st->buffered;
        memcpy(st->buffer + st->buffered, p, fill);
        st->t[0] += 128;
        st->t[1] += st->t[0] < 128;
        mel__blake2b_compress(st->h, st->buffer, st->t[0], st->t[1], 0);
        st->buffered = 0;
        p += fill;
        len -= fill;

        while (len > 128)
        {
            st->t[0] += 128;
            st->t[1] += st->t[0] < 128;
            mel__blake2b_compress(st->h, p, st->t[0], st->t[1], 0);
            p += 128;
            len -= 128;
        }
    }

    memcpy(st->buffer + st->buffered, p, len);
    st->buffered += len;
}

void mel_blake2b_final(const Mel_Blake2b_State* st, void* out)
{
    u64 h[8];
    memcpy(h, st->h, sizeof(h));

    u8 block[128];
    memcpy(block, st->buffer, st->buffered);
    memset(block + st->buffered, 0, 128 - st->buffered);

    u64 t0 = st->t[0] + st->buffered;
    u64 t1 = st->t[1] + (t0 < st->buffered);
    mel__blake2b_compress(h, block, t0, t1, ~(u64)0);

    u8 full[64];
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            full[8 * i + j] = (u8)(h[i] >> (8 * j));
    memcpy(out, full, st->out_len);
}

void mel_blake2b(void* out, usize out_len, const void* data, usize len, const void* key, usize key_len)
{
    Mel_Blake2b_State st;
    mel_blake2b_init(&st, out_len, key, key_len);
    mel_blake2b_update(&st, data, len);
    mel_blake2b_final(&st, out);
}

void mel_blake2s_init(Mel_Blake2s_State* st, usize out_len, const void* key, usize key_len)
{
    assert(out_len >= 1 && out_len <= 32);
    assert(key_len <= 32);

    memcpy(st->h, mel__blake2s_iv, sizeof(st->h));
    st->h[0] ^= 0x01010000U ^ ((u32)key_len << 8) ^ (u32)out_len;
    st->t[0] = 0;
    st->t[1] = 0;
    st->buffered = 0;
    st->out_len = out_len;

    if (key_len)
    {
        memset(st->buffer, 0, sizeof(st->buffer));
        memcpy(st->buffer, key, key_len);
        st->buffered = 64;
    }
}

void mel_blake2s_update(Mel_Blake2s_State* st, const void* data, usize len)
{
    if (!len)
        return;

    const u8* p = (const u8*)data;
    if (st->buffered + len > 64)
    {
        usize fill = 64 - st->buffered;
        memcpy(st->buffer + st->buffered, p, fill);
        st->t[0] += 64;
        st->t[1] += st->t[0] < 64;
        mel__blake2s_compress(st->h, st->buffer, st->t[0], st->t[1], 0);
        st->buffered = 0;
        p += fill;
        len -= fill;

        while (len > 64)
        {
            st->t[0] += 64;
            st->t[1] += st->t[0] < 64;
            mel__blake2s_compress(st->h, p, st->t[0], st->t[1], 0);
            p += 64;
            len -= 64;
        }
    }

    memcpy(st->buffer + st->buffered, p, len);
    st->buffered += len;
}

void mel_blake2s_final(const Mel_Blake2s_State* st, void* out)
{
    u32 h[8];
    memcpy(h, st->h, sizeof(h));

    u8 block[64];
    memcpy(block, st->buffer, st->buffered);
    memset(block + st->buffered, 0, 64 - st->buffered);

    u32 t0 = st->t[0] + (u32)st->buffered;
    u32 t1 = st->t[1] + (t0 < (u32)st->buffered);
    mel__blake2s_compress(h, block, t0, t1, ~(u32)0);

    u8 full[32];
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++)
            full[4 * i + j] = (u8)(h[i] >> (8 * j));
    memcpy(out, full, st->out_len);
}

void mel_blake2s(void* out, usize out_len, const void* data, usize len, const void* key, usize key_len)
{
    Mel_Blake2s_State st;
    mel_blake2s_init(&st, out_len, key, key_len);
    mel_blake2s_update(&st, data, len);
    mel_blake2s_final(&st, out);
}
