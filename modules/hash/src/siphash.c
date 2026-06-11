#include <hash/siphash.h>
#include <string.h>

static inline u64 mel__sip_rotl64(u64 v, int n) { return (v << n) | (v >> (64 - n)); }

static inline u64 mel__sip_read64(const void* p)
{
    u64 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

#define SIP_ROUND(v0, v1, v2, v3)     \
    do                                \
    {                                 \
        v0 += v1;                     \
        v1 = mel__sip_rotl64(v1, 13); \
        v1 ^= v0;                     \
        v0 = mel__sip_rotl64(v0, 32); \
        v2 += v3;                     \
        v3 = mel__sip_rotl64(v3, 16); \
        v3 ^= v2;                     \
        v0 += v3;                     \
        v3 = mel__sip_rotl64(v3, 21); \
        v3 ^= v0;                     \
        v2 += v1;                     \
        v1 = mel__sip_rotl64(v1, 17); \
        v1 ^= v2;                     \
        v2 = mel__sip_rotl64(v2, 32); \
    } while (0)

u64 mel_siphash24(const void* data, usize len, u64 k0, u64 k1)
{
    u64 v0 = 0x736F6D6570736575ULL ^ k0;
    u64 v1 = 0x646F72616E646F6DULL ^ k1;
    u64 v2 = 0x6C7967656E657261ULL ^ k0;
    u64 v3 = 0x7465646279746573ULL ^ k1;

    const u8* p = (const u8*)data;
    const u8* end = p + (len & ~(usize)7);

    while (p != end)
    {
        u64 m = mel__sip_read64(p);
        v3 ^= m;
        SIP_ROUND(v0, v1, v2, v3);
        SIP_ROUND(v0, v1, v2, v3);
        v0 ^= m;
        p += 8;
    }

    u64 b = (u64)len << 56;
    switch (len & 7)
    {
    case 7:
        b |= (u64)p[6] << 48;
    case 6:
        b |= (u64)p[5] << 40;
    case 5:
        b |= (u64)p[4] << 32;
    case 4:
        b |= (u64)p[3] << 24;
    case 3:
        b |= (u64)p[2] << 16;
    case 2:
        b |= (u64)p[1] << 8;
    case 1:
        b |= (u64)p[0];
    }

    v3 ^= b;
    SIP_ROUND(v0, v1, v2, v3);
    SIP_ROUND(v0, v1, v2, v3);
    v0 ^= b;

    v2 ^= 0xFF;
    SIP_ROUND(v0, v1, v2, v3);
    SIP_ROUND(v0, v1, v2, v3);
    SIP_ROUND(v0, v1, v2, v3);
    SIP_ROUND(v0, v1, v2, v3);

    return v0 ^ v1 ^ v2 ^ v3;
}
