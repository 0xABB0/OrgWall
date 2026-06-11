#include <test/test.h>

#include <hash/crc32.h>
#include <hash/fnv.h>
#include <hash/murmur3.h>
#include <hash/siphash.h>
#include <hash/xxh.h>

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
