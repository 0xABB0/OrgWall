#include <test/test.h>

#include <hash/crc32.h>
#include <hash/fnv.h>
#include <hash/mix.h>
#include <hash/murmur3.h>
#include <hash/siphash.h>
#include <hash/xxh.h>

#include "xxh3_vectors.h"

#define MEL__HASH_FILL_MAX 5000

static void mel__hash_fill(u8* buf, usize len)
{
    for (usize i = 0; i < len; i++)
        buf[i] = (u8)(((u32)(i * 2654435761U)) >> 24);
}

MEL_TEST(hash, fnv1a32_vectors)
{
    MEL_EXPECT_EQ(mel_fnv1a32("", 0), 0x811C9DC5U);
    MEL_EXPECT_EQ(mel_fnv1a32("a", 1), 0xE40C292CU);
    MEL_EXPECT_EQ(mel_fnv1a32("foobar", 6), 0xBF9CF968U);
}

MEL_TEST(hash, fnv1a64_vectors)
{
    MEL_EXPECT_EQ(mel_fnv1a64("", 0), 0xCBF29CE484222325ULL);
    MEL_EXPECT_EQ(mel_fnv1a64("a", 1), 0xAF63DC4C8601EC8CULL);
    MEL_EXPECT_EQ(mel_fnv1a64("foobar", 6), 0x85944171F73967E8ULL);
}

MEL_TEST(hash, crc32_vectors)
{
    MEL_EXPECT_EQ(mel_crc32("", 0), 0x00000000U);
    MEL_EXPECT_EQ(mel_crc32("123456789", 9), 0xCBF43926U);
    MEL_EXPECT_EQ(mel_crc32("The quick brown fox jumps over the lazy dog", 43), 0x414FA339U);
}

MEL_TEST(hash, crc32_update_chains)
{
    u32 crc = mel_crc32_update(0, "1234", 4);
    crc = mel_crc32_update(crc, "56789", 5);
    MEL_EXPECT_EQ(crc, 0xCBF43926U);
}

MEL_TEST(hash, crc32c_vectors)
{
    MEL_EXPECT_EQ(mel_crc32c("", 0), 0x00000000U);
    MEL_EXPECT_EQ(mel_crc32c("123456789", 9), 0xE3069283U);
}

MEL_TEST(hash, crc32c_update_chains)
{
    u32 crc = mel_crc32c_update(0, "12345", 5);
    crc = mel_crc32c_update(crc, "6789", 4);
    MEL_EXPECT_EQ(crc, 0xE3069283U);
}

MEL_TEST(hash, murmur3_32_vectors)
{
    u8 bytes[] = { 0x21, 0x43, 0x65, 0x87 };
    MEL_EXPECT_EQ(mel_murmur3_32("", 0, 0), 0x00000000U);
    MEL_EXPECT_EQ(mel_murmur3_32("", 0, 1), 0x514E28B7U);
    MEL_EXPECT_EQ(mel_murmur3_32("", 0, 0xFFFFFFFFU), 0x81F16F39U);
    MEL_EXPECT_EQ(mel_murmur3_32(bytes, 4, 0), 0xF55B516BU);
    MEL_EXPECT_EQ(mel_murmur3_32("aaaa", 4, 0x9747B28CU), 0x5A97808AU);
    MEL_EXPECT_EQ(mel_murmur3_32("Hello, world!", 13, 0x9747B28CU), 0x24884CBAU);
}

MEL_TEST(hash, siphash24_reference_vectors)
{
    u64 k0 = 0x0706050403020100ULL;
    u64 k1 = 0x0F0E0D0C0B0A0908ULL;
    u8  msg[15];
    for (usize i = 0; i < sizeof(msg); i++)
        msg[i] = (u8)i;

    MEL_EXPECT_EQ(mel_siphash24(msg, 0, k0, k1), 0x726FDB47DD0E0E31ULL);
    MEL_EXPECT_EQ(mel_siphash24(msg, 8, k0, k1), 0x93F5F5799A932462ULL);
    MEL_EXPECT_EQ(mel_siphash24(msg, 15, k0, k1), 0xA129CA6149BE45E5ULL);
}

MEL_TEST(hash, xxh64_vectors) { MEL_EXPECT_EQ(mel_xxh64("", 0, 0), 0xEF46DB3751D8E999ULL); }

MEL_TEST(hash, xxh3_seed_zero_matches_unseeded)
{
    const char* msg = "The quick brown fox jumps over the lazy dog";
    MEL_EXPECT_EQ(mel_xxh3_64_seeded(msg, 43, 0), mel_xxh3_64(msg, 43));
}

MEL_TEST(hash, xxh3_64_reference_vectors)
{
    static u8 buf[MEL__HASH_FILL_MAX];
    mel__hash_fill(buf, sizeof(buf));
    for (usize i = 0; i < (usize)countof(mel__xxh3_vectors); i++)
    {
        const Mel__Xxh3_Vector* v = &mel__xxh3_vectors[i];
        MEL_EXPECT_EQ(mel_xxh3_64_seeded(buf, v->len, v->seed), v->h64);
        if (v->seed == 0)
            MEL_EXPECT_EQ(mel_xxh3_64(buf, v->len), v->h64);
    }
}

MEL_TEST(hash, xxh3_128_reference_vectors)
{
    static u8 buf[MEL__HASH_FILL_MAX];
    mel__hash_fill(buf, sizeof(buf));
    for (usize i = 0; i < (usize)countof(mel__xxh3_vectors); i++)
    {
        const Mel__Xxh3_Vector* v = &mel__xxh3_vectors[i];
        Mel_Xxh128              h = mel_xxh3_128_seeded(buf, v->len, v->seed);
        MEL_EXPECT_EQ(h.low, v->low128);
        MEL_EXPECT_EQ(h.high, v->high128);
        if (v->seed == 0)
        {
            Mel_Xxh128 u = mel_xxh3_128(buf, v->len);
            MEL_EXPECT_EQ(u.low, v->low128);
            MEL_EXPECT_EQ(u.high, v->high128);
        }
    }
}

MEL_TEST(hash, xxh3_streaming_matches_oneshot)
{
    static u8 buf[MEL__HASH_FILL_MAX];
    mel__hash_fill(buf, sizeof(buf));
    usize chunk_sizes[] = { 1, 7, 64, 256, 1000 };
    for (usize c = 0; c < (usize)countof(chunk_sizes); c++)
    {
        for (usize i = 0; i < (usize)countof(mel__xxh3_vectors); i++)
        {
            const Mel__Xxh3_Vector* v = &mel__xxh3_vectors[i];
            Mel_Xxh3_State          st;
            mel_xxh3_init_seeded(&st, v->seed);
            for (usize fed = 0; fed < v->len; fed += chunk_sizes[c])
            {
                usize n = v->len - fed < chunk_sizes[c] ? v->len - fed : chunk_sizes[c];
                mel_xxh3_update(&st, buf + fed, n);
            }
            MEL_EXPECT_EQ(mel_xxh3_final_64(&st), v->h64);
            Mel_Xxh128 h = mel_xxh3_final_128(&st);
            MEL_EXPECT_EQ(h.low, v->low128);
            MEL_EXPECT_EQ(h.high, v->high128);
        }
    }
}

MEL_TEST(hash, mix_vectors)
{
    MEL_EXPECT_EQ(mel_hash_mix64(0x9E3779B97F4A7C15ULL), 0xE220A8397B1DCDAFULL);
    MEL_EXPECT_EQ(mel_hash_mix64(1), 0x5692161D100B05E5ULL);
    MEL_EXPECT_EQ(mel_hash_mix32(1), 0x514E28B7U);
    MEL_EXPECT_EQ(mel_hash_mix32(0xDEADBEEFU), 0x0DE5C6A9U);
    MEL_EXPECT_EQ(mel_hash_combine64(0, 1), 0x910A2DEC89025CC1ULL);
    MEL_EXPECT_EQ(mel_hash_combine64(1, 0), 0x8C741196ACC47E35ULL);
    MEL_EXPECT_EQ(mel_hash_combine64(mel_hash_combine64(0, 1), 2), 0x35EAEB1C84D55087ULL);
}
