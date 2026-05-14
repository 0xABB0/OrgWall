#include <hash/xxh.h>
#include <string.h>

static inline u64 mel__xxh_rotl64(u64 v, int n)
{
    return (v << n) | (v >> (64 - n));
}

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

static inline u32 mel__xxh_swap32(u32 x)
{
    return ((x << 24) & 0xFF000000) |
           ((x <<  8) & 0x00FF0000) |
           ((x >>  8) & 0x0000FF00) |
           ((x >> 24) & 0x000000FF);
}

static inline u64 mel__xxh_swap64(u64 x)
{
    return ((x << 56) & 0xFF00000000000000ULL) |
           ((x << 40) & 0x00FF000000000000ULL) |
           ((x << 24) & 0x0000FF0000000000ULL) |
           ((x <<  8) & 0x000000FF00000000ULL) |
           ((x >>  8) & 0x00000000FF000000ULL) |
           ((x >> 24) & 0x0000000000FF0000ULL) |
           ((x >> 40) & 0x000000000000FF00ULL) |
           ((x >> 56) & 0x00000000000000FFULL);
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
    u64 h64;

    if (len >= 32) {
        u64 v1 = seed + XXH_PRIME64_1 + XXH_PRIME64_2;
        u64 v2 = seed + XXH_PRIME64_2;
        u64 v3 = seed + 0;
        u64 v4 = seed - XXH_PRIME64_1;

        const u8* limit = p + len - 31;
        do {
            v1 = mel__xxh64_round(v1, mel__xxh_read64(p));      p += 8;
            v2 = mel__xxh64_round(v2, mel__xxh_read64(p));      p += 8;
            v3 = mel__xxh64_round(v3, mel__xxh_read64(p));      p += 8;
            v4 = mel__xxh64_round(v4, mel__xxh_read64(p));      p += 8;
        } while (p < limit);

        h64 = mel__xxh_rotl64(v1, 1) + mel__xxh_rotl64(v2, 7)
            + mel__xxh_rotl64(v3, 12) + mel__xxh_rotl64(v4, 18);

        h64 = mel__xxh64_merge_round(h64, v1);
        h64 = mel__xxh64_merge_round(h64, v2);
        h64 = mel__xxh64_merge_round(h64, v3);
        h64 = mel__xxh64_merge_round(h64, v4);
    } else {
        h64 = seed + XXH_PRIME64_5;
    }

    h64 += (u64)len;

    usize remaining = len & 31;
    while (remaining >= 8) {
        u64 k1 = mel__xxh64_round(0, mel__xxh_read64(p));
        p += 8;
        h64 ^= k1;
        h64 = mel__xxh_rotl64(h64, 27) * XXH_PRIME64_1 + XXH_PRIME64_4;
        remaining -= 8;
    }
    if (remaining >= 4) {
        h64 ^= (u64)mel__xxh_read32(p) * XXH_PRIME64_1;
        p += 4;
        h64 = mel__xxh_rotl64(h64, 23) * XXH_PRIME64_2 + XXH_PRIME64_3;
        remaining -= 4;
    }
    while (remaining > 0) {
        h64 ^= (u64)(*p++) * XXH_PRIME64_5;
        h64 = mel__xxh_rotl64(h64, 11) * XXH_PRIME64_1;
        --remaining;
    }

    return mel__xxh64_avalanche(h64);
}

#define XXH_SECRET_DEFAULT_SIZE 192
#define XXH_STRIPE_LEN 64
#define XXH_ACC_NB (XXH_STRIPE_LEN / sizeof(u64))
#define XXH_SECRET_CONSUME_RATE 8
#define XXH_SECRET_MERGEACCS_START 11
#define XXH_SECRET_LASTACC_START 7
#define XXH3_MIDSIZE_MAX 240
#define XXH3_MIDSIZE_STARTOFFSET 3
#define XXH3_MIDSIZE_LASTOFFSET 17
#define XXH3_SECRET_SIZE_MIN 136

#define PRIME_MX1 0x165667919E3779F9ULL
#define PRIME_MX2 0x9FB21C651E98DF25ULL

static const u8 mel__xxh3_secret[XXH_SECRET_DEFAULT_SIZE] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
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
    return mel__xxh3_mul128_fold64(
        input_lo ^ (mel__xxh_read64(secret) + seed),
        input_hi ^ (mel__xxh_read64(secret + 8) - seed)
    );
}

static inline u64 mel__xxh3_len_1to3(const u8* input, usize len, const u8* secret, u64 seed)
{
    u8 c1 = input[0];
    u8 c2 = input[len >> 1];
    u8 c3 = input[len - 1];
    u32 combined = ((u32)c1 << 16) | ((u32)c2 << 24)
                 | ((u32)c3 <<  0) | ((u32)len << 8);
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
    u64 acc = len
            + mel__xxh_swap64(input_lo) + input_hi
            + mel__xxh3_mul128_fold64(input_lo, input_hi);
    return mel__xxh3_avalanche(acc);
}

static inline u64 mel__xxh3_len_0to16(const u8* input, usize len, const u8* secret, u64 seed)
{
    if (len > 8)  return mel__xxh3_len_9to16(input, len, secret, seed);
    if (len >= 4) return mel__xxh3_len_4to8(input, len, secret, seed);
    if (len)      return mel__xxh3_len_1to3(input, len, secret, seed);
    return mel__xxh64_avalanche(seed ^ (mel__xxh_read64(secret + 56) ^ mel__xxh_read64(secret + 64)));
}

static inline u64 mel__xxh3_len_17to128(const u8* input, usize len, const u8* secret, u64 seed)
{
    u64 acc = len * XXH_PRIME64_1;

    if (len > 32) {
        if (len > 64) {
            if (len > 96) {
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

    for (u32 i = 0; i < 8; i++) {
        acc += mel__xxh3_mix16b(input + (16 * i), secret + (16 * i), seed);
    }

    u64 acc_end = mel__xxh3_mix16b(input + len - 16, secret + XXH3_SECRET_SIZE_MIN - XXH3_MIDSIZE_LASTOFFSET, seed);
    acc = mel__xxh3_avalanche(acc);

    for (u32 i = 8; i < nb_rounds; i++) {
        acc_end += mel__xxh3_mix16b(input + (16 * i), secret + (16 * (i - 8)) + XXH3_MIDSIZE_STARTOFFSET, seed);
    }

    return mel__xxh3_avalanche(acc + acc_end);
}

static inline void mel__xxh3_accumulate_512(u64* acc, const u8* input, const u8* secret)
{
    for (usize i = 0; i < XXH_ACC_NB; i++) {
        u64 data_val = mel__xxh_read64(input + i * 8);
        u64 data_key = data_val ^ mel__xxh_read64(secret + i * 8);
        acc[i ^ 1] += data_val;
        acc[i] += (u64)(u32)data_key * (u64)(data_key >> 32);
    }
}

static inline void mel__xxh3_scramble_acc(u64* acc, const u8* secret)
{
    for (usize i = 0; i < XXH_ACC_NB; i++) {
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
    for (usize n = 0; n < nb_stripes; n++) {
        mel__xxh3_accumulate_512(acc, input + n * XXH_STRIPE_LEN, secret + n * XXH_SECRET_CONSUME_RATE);
    }
}

static inline u64 mel__xxh3_mix2accs(const u64* acc, const u8* secret)
{
    return mel__xxh3_mul128_fold64(
        acc[0] ^ mel__xxh_read64(secret),
        acc[1] ^ mel__xxh_read64(secret + 8)
    );
}

static inline u64 mel__xxh3_merge_accs(const u64* acc, const u8* secret, u64 start)
{
    u64 result = start;
    for (usize i = 0; i < 4; i++) {
        result += mel__xxh3_mix2accs(acc + 2 * i, secret + 16 * i);
    }
    return mel__xxh3_avalanche(result);
}

static inline void mel__xxh3_init_custom_secret(u8* custom_secret, u64 seed)
{
    for (int i = 0; i < XXH_SECRET_DEFAULT_SIZE / 16; i++) {
        u64 lo = mel__xxh_read64(mel__xxh3_secret + 16 * i) + seed;
        u64 hi = mel__xxh_read64(mel__xxh3_secret + 16 * i + 8) - seed;
        memcpy(custom_secret + 16 * i, &lo, sizeof(lo));
        memcpy(custom_secret + 16 * i + 8, &hi, sizeof(hi));
    }
}

static u64 mel__xxh3_hash_long(const void* input, usize len, const u8* secret, usize secret_size)
{
    _Alignas(64) u64 acc[XXH_ACC_NB] = {
        XXH_PRIME32_3, XXH_PRIME64_1, XXH_PRIME64_2, XXH_PRIME64_3,
        XXH_PRIME64_4, XXH_PRIME32_2, XXH_PRIME64_5, XXH_PRIME32_1
    };

    const u8* p = (const u8*)input;
    usize nb_stripes_per_block = (secret_size - XXH_STRIPE_LEN) / XXH_SECRET_CONSUME_RATE;
    usize block_len = XXH_STRIPE_LEN * nb_stripes_per_block;
    usize nb_blocks = (len - 1) / block_len;

    for (usize n = 0; n < nb_blocks; n++) {
        mel__xxh3_accumulate(acc, p + n * block_len, secret, nb_stripes_per_block);
        mel__xxh3_scramble_acc(acc, secret + secret_size - XXH_STRIPE_LEN);
    }

    usize nb_stripes = ((len - 1) - (block_len * nb_blocks)) / XXH_STRIPE_LEN;
    mel__xxh3_accumulate(acc, p + nb_blocks * block_len, secret, nb_stripes);

    const u8* last_stripe = p + len - XXH_STRIPE_LEN;
    mel__xxh3_accumulate_512(acc, last_stripe, secret + secret_size - XXH_STRIPE_LEN - XXH_SECRET_LASTACC_START);

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

u64 mel_xxh3_64(const void* data, usize len)
{
    return mel__xxh3_64_internal(data, len, 0, mel__xxh3_secret, sizeof(mel__xxh3_secret));
}

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
