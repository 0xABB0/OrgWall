#include <hash/murmur3.h>
#include <string.h>

#define MURMUR3_C1 0xCC9E2D51U
#define MURMUR3_C2 0x1B873593U

static inline u32 mel__murmur3_rotl32(u32 v, int n) { return (v << n) | (v >> (32 - n)); }

static inline u32 mel__murmur3_read32(const void* p)
{
    u32 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static inline u32 mel__murmur3_fmix32(u32 h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6BU;
    h ^= h >> 13;
    h *= 0xC2B2AE35U;
    h ^= h >> 16;
    return h;
}

u32 mel_murmur3_32(const void* data, usize len, u32 seed)
{
    const u8* p = (const u8*)data;
    usize     nblocks = len / 4;
    u32       h = seed;

    for (usize i = 0; i < nblocks; i++)
    {
        u32 k = mel__murmur3_read32(p + i * 4);
        k *= MURMUR3_C1;
        k = mel__murmur3_rotl32(k, 15);
        k *= MURMUR3_C2;
        h ^= k;
        h = mel__murmur3_rotl32(h, 13);
        h = h * 5 + 0xE6546B64U;
    }

    const u8* tail = p + nblocks * 4;
    u32       k = 0;
    switch (len & 3)
    {
    case 3:
        k ^= (u32)tail[2] << 16;
    case 2:
        k ^= (u32)tail[1] << 8;
    case 1:
        k ^= (u32)tail[0];
        k *= MURMUR3_C1;
        k = mel__murmur3_rotl32(k, 15);
        k *= MURMUR3_C2;
        h ^= k;
    }

    h ^= (u32)len;
    return mel__murmur3_fmix32(h);
}
