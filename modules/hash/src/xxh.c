#include <hash/xxh.h>
#include <string.h>

static inline u64 mel__xxh_rotl64(u64 v, int n) { return (v << n) | (v >> (64 - n)); }

static inline u32 mel__xxh_rotl32(u32 v, int n) { return (v << n) | (v >> (32 - n)); }

static inline u32 mel__xxh_read32(const void* p)
{
    u32 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static inline u64 mel__xxh_read64(const void* p)
{
    u64 v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static inline u32 mel__xxh_swap32(u32 x) { return ((x << 24) & 0xFF000000) | ((x << 8) & 0x00FF0000) | ((x >> 8) & 0x0000FF00) | ((x >> 24) & 0x000000FF); }

static inline u64 mel__xxh_swap64(u64 x)
{
    return ((x << 56) & 0xFF00000000000000ULL) | ((x << 40) & 0x00FF000000000000ULL) | ((x << 24) & 0x0000FF0000000000ULL) | ((x << 8) & 0x000000FF00000000ULL) | ((x >> 8) & 0x00000000FF000000ULL) | ((x >> 24) & 0x0000000000FF0000ULL) |
           ((x >> 40) & 0x000000000000FF00ULL) | ((x >> 56) & 0x00000000000000FFULL);
}

#define XXH_PRIME64_1 0x9E3779B185EBCA87ULL
#define XXH_PRIME64_2 0xC2B2AE3D27D4EB4FULL
#define XXH_PRIME64_3 0x165667B19E3779F9ULL
#define XXH_PRIME64_4 0x85EBCA77C2B2AE63ULL
#define XXH_PRIME64_5 0x27D4EB2F165667C5ULL

#define XXH_PRIME32_1 0x9E3779B1U
#define XXH_PRIME32_2 0x85EBCA77U
#define XXH_PRIME32_3 0xC2B2AE3DU

static inline u64 mel__xxh64_round(u64 acc, u64 input)
{
    acc += input * XXH_PRIME64_2;
    acc = mel__xxh_rotl64(acc, 31);
    acc *= XXH_PRIME64_1;
    return acc;
}

static inline u64 mel__xxh64_merge_round(u64 acc, u64 val)
{
    val = mel__xxh64_round(0, val);
    acc ^= val;
    acc = acc * XXH_PRIME64_1 + XXH_PRIME64_4;
    return acc;
}

static inline u64 mel__xxh64_avalanche(u64 hash)
{
    hash ^= hash >> 33;
    hash *= XXH_PRIME64_2;
    hash ^= hash >> 29;
    hash *= XXH_PRIME64_3;
    hash ^= hash >> 32;
    return hash;
}

u64 mel_xxh64(const void* data, usize len, u64 seed)
{
    const u8* p = (const u8*)data;
    u64       h64;

    if (len >= 32)
    {
        u64 v1 = seed + XXH_PRIME64_1 + XXH_PRIME64_2;
        u64 v2 = seed + XXH_PRIME64_2;
        u64 v3 = seed + 0;
        u64 v4 = seed - XXH_PRIME64_1;

        const u8* limit = p + len - 31;
        do
        {
            v1 = mel__xxh64_round(v1, mel__xxh_read64(p));
            p += 8;
            v2 = mel__xxh64_round(v2, mel__xxh_read64(p));
            p += 8;
            v3 = mel__xxh64_round(v3, mel__xxh_read64(p));
            p += 8;
            v4 = mel__xxh64_round(v4, mel__xxh_read64(p));
            p += 8;
        } while (p < limit);

        h64 = mel__xxh_rotl64(v1, 1) + mel__xxh_rotl64(v2, 7) + mel__xxh_rotl64(v3, 12) + mel__xxh_rotl64(v4, 18);

        h64 = mel__xxh64_merge_round(h64, v1);
        h64 = mel__xxh64_merge_round(h64, v2);
        h64 = mel__xxh64_merge_round(h64, v3);
        h64 = mel__xxh64_merge_round(h64, v4);
    }
    else
    {
        h64 = seed + XXH_PRIME64_5;
    }

    h64 += (u64)len;

    usize remaining = len & 31;
    while (remaining >= 8)
    {
        u64 k1 = mel__xxh64_round(0, mel__xxh_read64(p));
        p += 8;
        h64 ^= k1;
        h64 = mel__xxh_rotl64(h64, 27) * XXH_PRIME64_1 + XXH_PRIME64_4;
        remaining -= 8;
    }
    if (remaining >= 4)
    {
        h64 ^= (u64)mel__xxh_read32(p) * XXH_PRIME64_1;
        p += 4;
        h64 = mel__xxh_rotl64(h64, 23) * XXH_PRIME64_2 + XXH_PRIME64_3;
        remaining -= 4;
    }
    while (remaining > 0)
    {
        h64 ^= (u64)(*p++) * XXH_PRIME64_5;
        h64 = mel__xxh_rotl64(h64, 11) * XXH_PRIME64_1;
        --remaining;
    }

    return mel__xxh64_avalanche(h64);
}

#define XXH_SECRET_DEFAULT_SIZE    192
#define XXH_STRIPE_LEN             64
#define XXH_ACC_NB                 (XXH_STRIPE_LEN / sizeof(u64))
#define XXH_SECRET_CONSUME_RATE    8
#define XXH_SECRET_MERGEACCS_START 11
#define XXH_SECRET_LASTACC_START   7
#define XXH3_MIDSIZE_MAX           240
#define XXH3_MIDSIZE_STARTOFFSET   3
#define XXH3_MIDSIZE_LASTOFFSET    17
#define XXH3_SECRET_SIZE_MIN       136

#define PRIME_MX1                  0x165667919E3779F9ULL
#define PRIME_MX2                  0x9FB21C651E98DF25ULL

static const u8 mel__xxh3_secret[XXH_SECRET_DEFAULT_SIZE] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c, 0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f, 0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5,
    0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21, 0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c, 0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53,
    0x2e, 0xa3, 0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8, 0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d, 0x8a, 0x51, 0xe0, 0x4b, 0xcd,
    0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64, 0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb, 0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26,
    0x29, 0xd4, 0x68, 0x9e, 0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce, 0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};

static inline u64 mel__xxh3_mul128_fold64(u64 lhs, u64 rhs)
{
    __uint128_t product = (__uint128_t)lhs * (__uint128_t)rhs;
    return (u64)product ^ (u64)(product >> 64);
}

static inline u64 mel__xxh3_avalanche(u64 h64)
{
    h64 ^= h64 >> 37;
    h64 *= PRIME_MX1;
    h64 ^= h64 >> 32;
    return h64;
}

static inline u64 mel__xxh3_rrmxmx(u64 h64, u64 len)
{
    h64 ^= mel__xxh_rotl64(h64, 49) ^ mel__xxh_rotl64(h64, 24);
    h64 *= PRIME_MX2;
    h64 ^= (h64 >> 35) + len;
    h64 *= PRIME_MX2;
    h64 ^= h64 >> 28;
    return h64;
}

static inline u64 mel__xxh3_mix16b(const u8* input, const u8* secret, u64 seed)
{
    u64 input_lo = mel__xxh_read64(input);
    u64 input_hi = mel__xxh_read64(input + 8);
    return mel__xxh3_mul128_fold64(input_lo ^ (mel__xxh_read64(secret) + seed), input_hi ^ (mel__xxh_read64(secret + 8) - seed));
}

static inline u64 mel__xxh3_len_1to3(const u8* input, usize len, const u8* secret, u64 seed)
{
    u8  c1 = input[0];
    u8  c2 = input[len >> 1];
    u8  c3 = input[len - 1];
    u32 combined = ((u32)c1 << 16) | ((u32)c2 << 24) | ((u32)c3 << 0) | ((u32)len << 8);
    u64 bitflip = (mel__xxh_read32(secret) ^ mel__xxh_read32(secret + 4)) + seed;
    u64 keyed = (u64)combined ^ bitflip;
    return mel__xxh64_avalanche(keyed);
}

static inline u64 mel__xxh3_len_4to8(const u8* input, usize len, const u8* secret, u64 seed)
{
    seed ^= (u64)mel__xxh_swap32((u32)seed) << 32;
    u32 input1 = mel__xxh_read32(input);
    u32 input2 = mel__xxh_read32(input + len - 4);
    u64 bitflip = (mel__xxh_read64(secret + 8) ^ mel__xxh_read64(secret + 16)) - seed;
    u64 input64 = input2 + ((u64)input1 << 32);
    u64 keyed = input64 ^ bitflip;
    return mel__xxh3_rrmxmx(keyed, len);
}

static inline u64 mel__xxh3_len_9to16(const u8* input, usize len, const u8* secret, u64 seed)
{
    u64 bitflip1 = (mel__xxh_read64(secret + 24) ^ mel__xxh_read64(secret + 32)) + seed;
    u64 bitflip2 = (mel__xxh_read64(secret + 40) ^ mel__xxh_read64(secret + 48)) - seed;
    u64 input_lo = mel__xxh_read64(input) ^ bitflip1;
    u64 input_hi = mel__xxh_read64(input + len - 8) ^ bitflip2;
    u64 acc = len + mel__xxh_swap64(input_lo) + input_hi + mel__xxh3_mul128_fold64(input_lo, input_hi);
    return mel__xxh3_avalanche(acc);
}

static inline u64 mel__xxh3_len_0to16(const u8* input, usize len, const u8* secret, u64 seed)
{
    if (len > 8)
        return mel__xxh3_len_9to16(input, len, secret, seed);
    if (len >= 4)
        return mel__xxh3_len_4to8(input, len, secret, seed);
    if (len)
        return mel__xxh3_len_1to3(input, len, secret, seed);
    return mel__xxh64_avalanche(seed ^ (mel__xxh_read64(secret + 56) ^ mel__xxh_read64(secret + 64)));
}

static inline u64 mel__xxh3_len_17to128(const u8* input, usize len, const u8* secret, u64 seed)
{
    u64 acc = len * XXH_PRIME64_1;

    if (len > 32)
    {
        if (len > 64)
        {
            if (len > 96)
            {
                acc += mel__xxh3_mix16b(input + 48, secret + 96, seed);
                acc += mel__xxh3_mix16b(input + len - 64, secret + 112, seed);
            }
            acc += mel__xxh3_mix16b(input + 32, secret + 64, seed);
            acc += mel__xxh3_mix16b(input + len - 48, secret + 80, seed);
        }
        acc += mel__xxh3_mix16b(input + 16, secret + 32, seed);
        acc += mel__xxh3_mix16b(input + len - 32, secret + 48, seed);
    }
    acc += mel__xxh3_mix16b(input + 0, secret + 0, seed);
    acc += mel__xxh3_mix16b(input + len - 16, secret + 16, seed);

    return mel__xxh3_avalanche(acc);
}

static inline u64 mel__xxh3_len_129to240(const u8* input, usize len, const u8* secret, u64 seed)
{
    u64 acc = len * XXH_PRIME64_1;
    u32 nb_rounds = (u32)len / 16;

    for (u32 i = 0; i < 8; i++)
    {
        acc += mel__xxh3_mix16b(input + (16 * i), secret + (16 * i), seed);
    }

    u64 acc_end = mel__xxh3_mix16b(input + len - 16, secret + XXH3_SECRET_SIZE_MIN - XXH3_MIDSIZE_LASTOFFSET, seed);
    acc = mel__xxh3_avalanche(acc);

    for (u32 i = 8; i < nb_rounds; i++)
    {
        acc_end += mel__xxh3_mix16b(input + (16 * i), secret + (16 * (i - 8)) + XXH3_MIDSIZE_STARTOFFSET, seed);
    }

    return mel__xxh3_avalanche(acc + acc_end);
}

static inline void mel__xxh3_accumulate_512(u64* acc, const u8* input, const u8* secret)
{
    for (usize i = 0; i < XXH_ACC_NB; i++)
    {
        u64 data_val = mel__xxh_read64(input + i * 8);
        u64 data_key = data_val ^ mel__xxh_read64(secret + i * 8);
        acc[i ^ 1] += data_val;
        acc[i] += (u64)(u32)data_key * (u64)(data_key >> 32);
    }
}

static inline void mel__xxh3_scramble_acc(u64* acc, const u8* secret)
{
    for (usize i = 0; i < XXH_ACC_NB; i++)
    {
        u64 key64 = mel__xxh_read64(secret + i * 8);
        u64 acc64 = acc[i];
        acc64 ^= acc64 >> 47;
        acc64 ^= key64;
        acc64 *= XXH_PRIME32_1;
        acc[i] = acc64;
    }
}

static inline void mel__xxh3_accumulate(u64* acc, const u8* input, const u8* secret, usize nb_stripes)
{
    for (usize n = 0; n < nb_stripes; n++)
    {
        mel__xxh3_accumulate_512(acc, input + n * XXH_STRIPE_LEN, secret + n * XXH_SECRET_CONSUME_RATE);
    }
}

static inline u64 mel__xxh3_mix2accs(const u64* acc, const u8* secret) { return mel__xxh3_mul128_fold64(acc[0] ^ mel__xxh_read64(secret), acc[1] ^ mel__xxh_read64(secret + 8)); }

static inline u64 mel__xxh3_merge_accs(const u64* acc, const u8* secret, u64 start)
{
    u64 result = start;
    for (usize i = 0; i < 4; i++)
    {
        result += mel__xxh3_mix2accs(acc + 2 * i, secret + 16 * i);
    }
    return mel__xxh3_avalanche(result);
}

static inline void mel__xxh3_init_custom_secret(u8* custom_secret, u64 seed)
{
    for (int i = 0; i < XXH_SECRET_DEFAULT_SIZE / 16; i++)
    {
        u64 lo = mel__xxh_read64(mel__xxh3_secret + 16 * i) + seed;
        u64 hi = mel__xxh_read64(mel__xxh3_secret + 16 * i + 8) - seed;
        memcpy(custom_secret + 16 * i, &lo, sizeof(lo));
        memcpy(custom_secret + 16 * i + 8, &hi, sizeof(hi));
    }
}

static const u64 mel__xxh3_acc_init[XXH_ACC_NB] = { XXH_PRIME32_3, XXH_PRIME64_1, XXH_PRIME64_2, XXH_PRIME64_3, XXH_PRIME64_4, XXH_PRIME32_2, XXH_PRIME64_5, XXH_PRIME32_1 };

static void mel__xxh3_hash_long_accum(u64* acc, const void* input, usize len, const u8* secret, usize secret_size)
{
    const u8* p = (const u8*)input;
    usize     nb_stripes_per_block = (secret_size - XXH_STRIPE_LEN) / XXH_SECRET_CONSUME_RATE;
    usize     block_len = XXH_STRIPE_LEN * nb_stripes_per_block;
    usize     nb_blocks = (len - 1) / block_len;

    for (usize n = 0; n < nb_blocks; n++)
    {
        mel__xxh3_accumulate(acc, p + n * block_len, secret, nb_stripes_per_block);
        mel__xxh3_scramble_acc(acc, secret + secret_size - XXH_STRIPE_LEN);
    }

    usize nb_stripes = ((len - 1) - (block_len * nb_blocks)) / XXH_STRIPE_LEN;
    mel__xxh3_accumulate(acc, p + nb_blocks * block_len, secret, nb_stripes);

    const u8* last_stripe = p + len - XXH_STRIPE_LEN;
    mel__xxh3_accumulate_512(acc, last_stripe, secret + secret_size - XXH_STRIPE_LEN - XXH_SECRET_LASTACC_START);
}

static u64 mel__xxh3_hash_long(const void* input, usize len, const u8* secret, usize secret_size)
{
    _Alignas(64) u64 acc[XXH_ACC_NB];
    memcpy(acc, mel__xxh3_acc_init, sizeof(acc));
    mel__xxh3_hash_long_accum(acc, input, len, secret, secret_size);
    return mel__xxh3_merge_accs(acc, secret + XXH_SECRET_MERGEACCS_START, (u64)len * XXH_PRIME64_1);
}

static u64 mel__xxh3_64_internal(const void* input, usize len, u64 seed, const u8* secret, usize secret_size)
{
    if (len <= 16)
        return mel__xxh3_len_0to16((const u8*)input, len, secret, seed);
    if (len <= 128)
        return mel__xxh3_len_17to128((const u8*)input, len, secret, seed);
    if (len <= XXH3_MIDSIZE_MAX)
        return mel__xxh3_len_129to240((const u8*)input, len, secret, seed);
    return mel__xxh3_hash_long(input, len, secret, secret_size);
}

u64 mel_xxh3_64(const void* data, usize len) { return mel__xxh3_64_internal(data, len, 0, mel__xxh3_secret, sizeof(mel__xxh3_secret)); }

u64 mel_xxh3_64_seeded(const void* data, usize len, u64 seed)
{
    if (len <= XXH3_MIDSIZE_MAX)
        return mel__xxh3_64_internal(data, len, seed, mel__xxh3_secret, sizeof(mel__xxh3_secret));

    if (seed == 0)
        return mel__xxh3_hash_long(data, len, mel__xxh3_secret, sizeof(mel__xxh3_secret));

    _Alignas(64) u8 custom_secret[XXH_SECRET_DEFAULT_SIZE];
    mel__xxh3_init_custom_secret(custom_secret, seed);
    return mel__xxh3_hash_long(data, len, custom_secret, sizeof(custom_secret));
}

static inline Mel_Xxh128 mel__xxh3_mul128(u64 lhs, u64 rhs)
{
    __uint128_t product = (__uint128_t)lhs * (__uint128_t)rhs;
    Mel_Xxh128  r = { (u64)product, (u64)(product >> 64) };
    return r;
}

static inline u64 mel__xxh3_xorshift64(u64 v, int shift) { return v ^ (v >> shift); }

static inline Mel_Xxh128 mel__xxh3_len_1to3_128(const u8* input, usize len, const u8* secret, u64 seed)
{
    u8         c1 = input[0];
    u8         c2 = input[len >> 1];
    u8         c3 = input[len - 1];
    u32        combined_lo = ((u32)c1 << 16) | ((u32)c2 << 24) | ((u32)c3 << 0) | ((u32)len << 8);
    u32        combined_hi = mel__xxh_rotl32(mel__xxh_swap32(combined_lo), 13);
    u64        bitflip_lo = (mel__xxh_read32(secret) ^ mel__xxh_read32(secret + 4)) + seed;
    u64        bitflip_hi = (mel__xxh_read32(secret + 8) ^ mel__xxh_read32(secret + 12)) - seed;
    Mel_Xxh128 h = { mel__xxh64_avalanche((u64)combined_lo ^ bitflip_lo), mel__xxh64_avalanche((u64)combined_hi ^ bitflip_hi) };
    return h;
}

static inline Mel_Xxh128 mel__xxh3_len_4to8_128(const u8* input, usize len, const u8* secret, u64 seed)
{
    seed ^= (u64)mel__xxh_swap32((u32)seed) << 32;
    u32        input_lo = mel__xxh_read32(input);
    u32        input_hi = mel__xxh_read32(input + len - 4);
    u64        input64 = input_lo + ((u64)input_hi << 32);
    u64        bitflip = (mel__xxh_read64(secret + 16) ^ mel__xxh_read64(secret + 24)) + seed;
    u64        keyed = input64 ^ bitflip;
    Mel_Xxh128 m128 = mel__xxh3_mul128(keyed, XXH_PRIME64_1 + (len << 2));

    m128.high += m128.low << 1;
    m128.low ^= m128.high >> 3;
    m128.low = mel__xxh3_xorshift64(m128.low, 35);
    m128.low *= PRIME_MX2;
    m128.low = mel__xxh3_xorshift64(m128.low, 28);
    m128.high = mel__xxh3_avalanche(m128.high);
    return m128;
}

static inline Mel_Xxh128 mel__xxh3_len_9to16_128(const u8* input, usize len, const u8* secret, u64 seed)
{
    u64        bitflip_lo = (mel__xxh_read64(secret + 32) ^ mel__xxh_read64(secret + 40)) - seed;
    u64        bitflip_hi = (mel__xxh_read64(secret + 48) ^ mel__xxh_read64(secret + 56)) + seed;
    u64        input_lo = mel__xxh_read64(input);
    u64        input_hi = mel__xxh_read64(input + len - 8);
    Mel_Xxh128 m128 = mel__xxh3_mul128(input_lo ^ input_hi ^ bitflip_lo, XXH_PRIME64_1);

    m128.low += (u64)(len - 1) << 54;
    input_hi ^= bitflip_hi;
    m128.high += input_hi + (u64)(u32)input_hi * (u64)(XXH_PRIME32_2 - 1);
    m128.low ^= mel__xxh_swap64(m128.high);

    Mel_Xxh128 h = mel__xxh3_mul128(m128.low, XXH_PRIME64_2);
    h.high += m128.high * XXH_PRIME64_2;
    h.low = mel__xxh3_avalanche(h.low);
    h.high = mel__xxh3_avalanche(h.high);
    return h;
}

static inline Mel_Xxh128 mel__xxh3_len_0to16_128(const u8* input, usize len, const u8* secret, u64 seed)
{
    if (len > 8)
        return mel__xxh3_len_9to16_128(input, len, secret, seed);
    if (len >= 4)
        return mel__xxh3_len_4to8_128(input, len, secret, seed);
    if (len)
        return mel__xxh3_len_1to3_128(input, len, secret, seed);
    u64        bitflip_lo = mel__xxh_read64(secret + 64) ^ mel__xxh_read64(secret + 72);
    u64        bitflip_hi = mel__xxh_read64(secret + 80) ^ mel__xxh_read64(secret + 88);
    Mel_Xxh128 h = { mel__xxh64_avalanche(seed ^ bitflip_lo), mel__xxh64_avalanche(seed ^ bitflip_hi) };
    return h;
}

static inline Mel_Xxh128 mel__xxh3_mix32b(Mel_Xxh128 acc, const u8* input1, const u8* input2, const u8* secret, u64 seed)
{
    acc.low += mel__xxh3_mix16b(input1, secret, seed);
    acc.low ^= mel__xxh_read64(input2) + mel__xxh_read64(input2 + 8);
    acc.high += mel__xxh3_mix16b(input2, secret + 16, seed);
    acc.high ^= mel__xxh_read64(input1) + mel__xxh_read64(input1 + 8);
    return acc;
}

static inline Mel_Xxh128 mel__xxh3_merge_128(Mel_Xxh128 acc, usize len, u64 seed)
{
    Mel_Xxh128 h;
    h.low = acc.low + acc.high;
    h.high = (acc.low * XXH_PRIME64_1) + (acc.high * XXH_PRIME64_4) + ((len - seed) * XXH_PRIME64_2);
    h.low = mel__xxh3_avalanche(h.low);
    h.high = (u64)0 - mel__xxh3_avalanche(h.high);
    return h;
}

static inline Mel_Xxh128 mel__xxh3_len_17to128_128(const u8* input, usize len, const u8* secret, u64 seed)
{
    Mel_Xxh128 acc = { len * XXH_PRIME64_1, 0 };

    if (len > 32)
    {
        if (len > 64)
        {
            if (len > 96)
            {
                acc = mel__xxh3_mix32b(acc, input + 48, input + len - 64, secret + 96, seed);
            }
            acc = mel__xxh3_mix32b(acc, input + 32, input + len - 48, secret + 64, seed);
        }
        acc = mel__xxh3_mix32b(acc, input + 16, input + len - 32, secret + 32, seed);
    }
    acc = mel__xxh3_mix32b(acc, input, input + len - 16, secret, seed);

    return mel__xxh3_merge_128(acc, len, seed);
}

static inline Mel_Xxh128 mel__xxh3_len_129to240_128(const u8* input, usize len, const u8* secret, u64 seed)
{
    Mel_Xxh128 acc = { len * XXH_PRIME64_1, 0 };

    for (usize i = 32; i < 160; i += 32)
    {
        acc = mel__xxh3_mix32b(acc, input + i - 32, input + i - 16, secret + i - 32, seed);
    }
    acc.low = mel__xxh3_avalanche(acc.low);
    acc.high = mel__xxh3_avalanche(acc.high);

    for (usize i = 160; i <= len; i += 32)
    {
        acc = mel__xxh3_mix32b(acc, input + i - 32, input + i - 16, secret + XXH3_MIDSIZE_STARTOFFSET + i - 160, seed);
    }
    acc = mel__xxh3_mix32b(acc, input + len - 16, input + len - 32, secret + XXH3_SECRET_SIZE_MIN - XXH3_MIDSIZE_LASTOFFSET - 16, (u64)0 - seed);

    return mel__xxh3_merge_128(acc, len, seed);
}

static Mel_Xxh128 mel__xxh3_hash_long_128(const void* input, usize len, const u8* secret, usize secret_size)
{
    _Alignas(64) u64 acc[XXH_ACC_NB];
    memcpy(acc, mel__xxh3_acc_init, sizeof(acc));
    mel__xxh3_hash_long_accum(acc, input, len, secret, secret_size);

    Mel_Xxh128 h;
    h.low = mel__xxh3_merge_accs(acc, secret + XXH_SECRET_MERGEACCS_START, (u64)len * XXH_PRIME64_1);
    h.high = mel__xxh3_merge_accs(acc, secret + secret_size - XXH_STRIPE_LEN - XXH_SECRET_MERGEACCS_START, ~((u64)len * XXH_PRIME64_2));
    return h;
}

static Mel_Xxh128 mel__xxh3_128_internal(const void* input, usize len, u64 seed, const u8* secret, usize secret_size)
{
    if (len <= 16)
        return mel__xxh3_len_0to16_128((const u8*)input, len, secret, seed);
    if (len <= 128)
        return mel__xxh3_len_17to128_128((const u8*)input, len, secret, seed);
    if (len <= XXH3_MIDSIZE_MAX)
        return mel__xxh3_len_129to240_128((const u8*)input, len, secret, seed);
    return mel__xxh3_hash_long_128(input, len, secret, secret_size);
}

Mel_Xxh128 mel_xxh3_128(const void* data, usize len) { return mel__xxh3_128_internal(data, len, 0, mel__xxh3_secret, sizeof(mel__xxh3_secret)); }

Mel_Xxh128 mel_xxh3_128_seeded(const void* data, usize len, u64 seed)
{
    if (len <= XXH3_MIDSIZE_MAX)
        return mel__xxh3_128_internal(data, len, seed, mel__xxh3_secret, sizeof(mel__xxh3_secret));

    if (seed == 0)
        return mel__xxh3_hash_long_128(data, len, mel__xxh3_secret, sizeof(mel__xxh3_secret));

    _Alignas(64) u8 custom_secret[XXH_SECRET_DEFAULT_SIZE];
    mel__xxh3_init_custom_secret(custom_secret, seed);
    return mel__xxh3_hash_long_128(data, len, custom_secret, sizeof(custom_secret));
}

#define XXH3_STREAM_BUFFER_SIZE    256
#define XXH3_STREAM_BUFFER_STRIPES (XXH3_STREAM_BUFFER_SIZE / XXH_STRIPE_LEN)
#define XXH3_STREAM_SECRET_LIMIT   (XXH_SECRET_DEFAULT_SIZE - XXH_STRIPE_LEN)
#define XXH3_STREAM_BLOCK_STRIPES  (XXH3_STREAM_SECRET_LIMIT / XXH_SECRET_CONSUME_RATE)

static void mel__xxh3_consume_stripes(u64* acc, usize* nb_stripes_so_far, const u8* input, usize nb_stripes, const u8* secret)
{
    const u8* initial_secret = secret + *nb_stripes_so_far * XXH_SECRET_CONSUME_RATE;
    if (nb_stripes >= XXH3_STREAM_BLOCK_STRIPES - *nb_stripes_so_far)
    {
        usize nb_stripes_this_iter = XXH3_STREAM_BLOCK_STRIPES - *nb_stripes_so_far;
        do
        {
            mel__xxh3_accumulate(acc, input, initial_secret, nb_stripes_this_iter);
            mel__xxh3_scramble_acc(acc, secret + XXH3_STREAM_SECRET_LIMIT);
            input += nb_stripes_this_iter * XXH_STRIPE_LEN;
            nb_stripes -= nb_stripes_this_iter;
            nb_stripes_this_iter = XXH3_STREAM_BLOCK_STRIPES;
            initial_secret = secret;
        } while (nb_stripes >= XXH3_STREAM_BLOCK_STRIPES);
        *nb_stripes_so_far = 0;
    }
    if (nb_stripes > 0)
    {
        mel__xxh3_accumulate(acc, input, initial_secret, nb_stripes);
        *nb_stripes_so_far += nb_stripes;
    }
}

static void mel__xxh3_state_init(Mel_Xxh3_State* st, u64 seed)
{
    memcpy(st->acc, mel__xxh3_acc_init, sizeof(st->acc));
    if (seed == 0)
        memcpy(st->secret, mel__xxh3_secret, sizeof(st->secret));
    else
        mel__xxh3_init_custom_secret(st->secret, seed);
    st->total_len = 0;
    st->seed = seed;
    st->nb_stripes_so_far = 0;
    st->buffered_size = 0;
}

void mel_xxh3_init(Mel_Xxh3_State* st) { mel__xxh3_state_init(st, 0); }

void mel_xxh3_init_seeded(Mel_Xxh3_State* st, u64 seed) { mel__xxh3_state_init(st, seed); }

void mel_xxh3_update(Mel_Xxh3_State* st, const void* data, usize len)
{
    const u8* p = (const u8*)data;
    const u8* end = p + len;

    st->total_len += len;

    if (st->buffered_size + len <= XXH3_STREAM_BUFFER_SIZE)
    {
        memcpy(st->buffer + st->buffered_size, p, len);
        st->buffered_size += (u32)len;
        return;
    }

    if (st->buffered_size)
    {
        usize load_size = XXH3_STREAM_BUFFER_SIZE - st->buffered_size;
        memcpy(st->buffer + st->buffered_size, p, load_size);
        p += load_size;
        mel__xxh3_consume_stripes(st->acc, &st->nb_stripes_so_far, st->buffer, XXH3_STREAM_BUFFER_STRIPES, st->secret);
        st->buffered_size = 0;
    }

    if (end - p > XXH3_STREAM_BUFFER_SIZE)
    {
        const u8* limit = end - XXH3_STREAM_BUFFER_SIZE;
        do
        {
            mel__xxh3_consume_stripes(st->acc, &st->nb_stripes_so_far, p, XXH3_STREAM_BUFFER_STRIPES, st->secret);
            p += XXH3_STREAM_BUFFER_SIZE;
        } while (p < limit);
        memcpy(st->buffer + XXH3_STREAM_BUFFER_SIZE - XXH_STRIPE_LEN, p - XXH_STRIPE_LEN, XXH_STRIPE_LEN);
    }

    memcpy(st->buffer, p, (usize)(end - p));
    st->buffered_size = (u32)(end - p);
}

static void mel__xxh3_digest_long(u64* acc, const Mel_Xxh3_State* st)
{
    memcpy(acc, st->acc, XXH_ACC_NB * sizeof(u64));

    if (st->buffered_size >= XXH_STRIPE_LEN)
    {
        usize nb_stripes = (st->buffered_size - 1) / XXH_STRIPE_LEN;
        usize nb_stripes_so_far = st->nb_stripes_so_far;
        mel__xxh3_consume_stripes(acc, &nb_stripes_so_far, st->buffer, nb_stripes, st->secret);
        mel__xxh3_accumulate_512(acc, st->buffer + st->buffered_size - XXH_STRIPE_LEN, st->secret + XXH3_STREAM_SECRET_LIMIT - XXH_SECRET_LASTACC_START);
    }
    else
    {
        u8    last_stripe[XXH_STRIPE_LEN];
        usize catchup_size = XXH_STRIPE_LEN - st->buffered_size;
        memcpy(last_stripe, st->buffer + XXH3_STREAM_BUFFER_SIZE - catchup_size, catchup_size);
        memcpy(last_stripe + catchup_size, st->buffer, st->buffered_size);
        mel__xxh3_accumulate_512(acc, last_stripe, st->secret + XXH3_STREAM_SECRET_LIMIT - XXH_SECRET_LASTACC_START);
    }
}

u64 mel_xxh3_final_64(const Mel_Xxh3_State* st)
{
    if (st->total_len <= XXH3_MIDSIZE_MAX)
        return mel__xxh3_64_internal(st->buffer, (usize)st->total_len, st->seed, mel__xxh3_secret, sizeof(mel__xxh3_secret));

    _Alignas(64) u64 acc[XXH_ACC_NB];
    mel__xxh3_digest_long(acc, st);
    return mel__xxh3_merge_accs(acc, st->secret + XXH_SECRET_MERGEACCS_START, st->total_len * XXH_PRIME64_1);
}

Mel_Xxh128 mel_xxh3_final_128(const Mel_Xxh3_State* st)
{
    if (st->total_len <= XXH3_MIDSIZE_MAX)
        return mel__xxh3_128_internal(st->buffer, (usize)st->total_len, st->seed, mel__xxh3_secret, sizeof(mel__xxh3_secret));

    _Alignas(64) u64 acc[XXH_ACC_NB];
    mel__xxh3_digest_long(acc, st);

    Mel_Xxh128 h;
    h.low = mel__xxh3_merge_accs(acc, st->secret + XXH_SECRET_MERGEACCS_START, st->total_len * XXH_PRIME64_1);
    h.high = mel__xxh3_merge_accs(acc, st->secret + sizeof(st->secret) - XXH_STRIPE_LEN - XXH_SECRET_MERGEACCS_START, ~(st->total_len * XXH_PRIME64_2));
    return h;
}
