#include <digest/sha3.h>
#include <string.h>

static inline u64 mel__keccak_rotl64(u64 v, int n) { return (v << n) | (v >> (64 - n)); }

static const u64 mel__keccak_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808AULL, 0x8000000080008000ULL, 0x000000000000808BULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008AULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000AULL, 0x000000008000808BULL, 0x800000000000008BULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800AULL, 0x800000008000000AULL, 0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL,
};

static const u8 mel__keccak_rotc[24] = { 1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44 };
static const u8 mel__keccak_piln[24] = { 10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1 };

static void mel__keccak_f1600(u64 a[25])
{
    for (int round = 0; round < 24; round++)
    {
        u64 c[5];
        for (int x = 0; x < 5; x++)
            c[x] = a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20];
        for (int x = 0; x < 5; x++)
        {
            u64 d = c[(x + 4) % 5] ^ mel__keccak_rotl64(c[(x + 1) % 5], 1);
            for (int y = 0; y < 25; y += 5)
                a[x + y] ^= d;
        }

        u64 t = a[1];
        for (int i = 0; i < 24; i++)
        {
            int j = mel__keccak_piln[i];
            u64 tmp = a[j];
            a[j] = mel__keccak_rotl64(t, mel__keccak_rotc[i]);
            t = tmp;
        }

        for (int y = 0; y < 25; y += 5)
        {
            u64 row[5];
            for (int x = 0; x < 5; x++)
                row[x] = a[y + x];
            for (int x = 0; x < 5; x++)
                a[y + x] = row[x] ^ (~row[(x + 1) % 5] & row[(x + 2) % 5]);
        }

        a[0] ^= mel__keccak_rc[round];
    }
}

static void mel__keccak_absorb(u64 a[25], u32* offset, u32 rate, const u8* p, usize len)
{
    u32 off = *offset;
    while (len)
    {
        usize take = rate - off;
        if (take > len)
            take = len;
        for (usize i = 0; i < take; i++)
        {
            u32 pos = off + (u32)i;
            a[pos >> 3] ^= (u64)p[i] << (8 * (pos & 7));
        }
        off += (u32)take;
        p += take;
        len -= take;
        if (off == rate)
        {
            mel__keccak_f1600(a);
            off = 0;
        }
    }
    *offset = off;
}

static void mel__keccak_pad(u64 a[25], u32 offset, u32 rate, u8 domain)
{
    a[offset >> 3] ^= (u64)domain << (8 * (offset & 7));
    a[(rate - 1) >> 3] ^= (u64)0x80 << (8 * ((rate - 1) & 7));
    mel__keccak_f1600(a);
}

static void mel__keccak_squeeze(u64 a[25], u32* offset, u32 rate, u8* out, usize out_len)
{
    u32 off = *offset;
    while (out_len)
    {
        if (off == rate)
        {
            mel__keccak_f1600(a);
            off = 0;
        }
        usize take = rate - off;
        if (take > out_len)
            take = out_len;
        for (usize i = 0; i < take; i++)
        {
            u32 pos = off + (u32)i;
            out[i] = (u8)(a[pos >> 3] >> (8 * (pos & 7)));
        }
        off += (u32)take;
        out += take;
        out_len -= take;
    }
    *offset = off;
}

static void mel__sha3_init(Mel_Sha3_State* st, u32 rate)
{
    memset(st->a, 0, sizeof(st->a));
    st->rate = rate;
    st->offset = 0;
}

void mel_sha3_224_init(Mel_Sha3_State* st) { mel__sha3_init(st, 144); }
void mel_sha3_256_init(Mel_Sha3_State* st) { mel__sha3_init(st, 136); }
void mel_sha3_384_init(Mel_Sha3_State* st) { mel__sha3_init(st, 104); }
void mel_sha3_512_init(Mel_Sha3_State* st) { mel__sha3_init(st, 72); }

void mel_sha3_update(Mel_Sha3_State* st, const void* data, usize len) { mel__keccak_absorb(st->a, &st->offset, st->rate, (const u8*)data, len); }

static void mel__sha3_digest(const Mel_Sha3_State* st, u8* out, usize out_len)
{
    u64 a[25];
    memcpy(a, st->a, sizeof(a));
    mel__keccak_pad(a, st->offset, st->rate, 0x06);
    u32 offset = 0;
    mel__keccak_squeeze(a, &offset, st->rate, out, out_len);
}

Mel_Sha3_224 mel_sha3_224_final(const Mel_Sha3_State* st)
{
    Mel_Sha3_224 out;
    mel__sha3_digest(st, out.bytes, sizeof(out.bytes));
    return out;
}

Mel_Sha3_256 mel_sha3_256_final(const Mel_Sha3_State* st)
{
    Mel_Sha3_256 out;
    mel__sha3_digest(st, out.bytes, sizeof(out.bytes));
    return out;
}

Mel_Sha3_384 mel_sha3_384_final(const Mel_Sha3_State* st)
{
    Mel_Sha3_384 out;
    mel__sha3_digest(st, out.bytes, sizeof(out.bytes));
    return out;
}

Mel_Sha3_512 mel_sha3_512_final(const Mel_Sha3_State* st)
{
    Mel_Sha3_512 out;
    mel__sha3_digest(st, out.bytes, sizeof(out.bytes));
    return out;
}

Mel_Sha3_224 mel_sha3_224(const void* data, usize len)
{
    Mel_Sha3_State st;
    mel_sha3_224_init(&st);
    mel_sha3_update(&st, data, len);
    return mel_sha3_224_final(&st);
}

Mel_Sha3_256 mel_sha3_256(const void* data, usize len)
{
    Mel_Sha3_State st;
    mel_sha3_256_init(&st);
    mel_sha3_update(&st, data, len);
    return mel_sha3_256_final(&st);
}

Mel_Sha3_384 mel_sha3_384(const void* data, usize len)
{
    Mel_Sha3_State st;
    mel_sha3_384_init(&st);
    mel_sha3_update(&st, data, len);
    return mel_sha3_384_final(&st);
}

Mel_Sha3_512 mel_sha3_512(const void* data, usize len)
{
    Mel_Sha3_State st;
    mel_sha3_512_init(&st);
    mel_sha3_update(&st, data, len);
    return mel_sha3_512_final(&st);
}

static void mel__shake_init(Mel_Shake_State* st, u32 rate)
{
    memset(st->a, 0, sizeof(st->a));
    st->rate = rate;
    st->offset = 0;
    st->squeezing = 0;
}

void mel_shake128_init(Mel_Shake_State* st) { mel__shake_init(st, 168); }
void mel_shake256_init(Mel_Shake_State* st) { mel__shake_init(st, 136); }

void mel_shake_update(Mel_Shake_State* st, const void* data, usize len)
{
    assert(!st->squeezing);
    mel__keccak_absorb(st->a, &st->offset, st->rate, (const u8*)data, len);
}

void mel_shake_squeeze(Mel_Shake_State* st, void* out, usize out_len)
{
    if (!st->squeezing)
    {
        mel__keccak_pad(st->a, st->offset, st->rate, 0x1F);
        st->offset = 0;
        st->squeezing = 1;
    }
    mel__keccak_squeeze(st->a, &st->offset, st->rate, (u8*)out, out_len);
}

void mel_shake128(const void* data, usize len, void* out, usize out_len)
{
    Mel_Shake_State st;
    mel_shake128_init(&st);
    mel_shake_update(&st, data, len);
    mel_shake_squeeze(&st, out, out_len);
}

void mel_shake256(const void* data, usize len, void* out, usize out_len)
{
    Mel_Shake_State st;
    mel_shake256_init(&st);
    mel_shake_update(&st, data, len);
    mel_shake_squeeze(&st, out, out_len);
}
