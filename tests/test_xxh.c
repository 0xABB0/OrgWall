#include "../melody/test.harness.h"
#include "../melody/hash.xxh.h"
#include <string.h>

MEL_TEST(xxh64_empty, .tags = "hash")
{
    MEL_ASSERT_EQ(mel_xxh64("", 0, 0), 0xEF46DB3751D8E999ULL);
}

MEL_TEST(xxh64_single_byte, .tags = "hash")
{
    MEL_ASSERT_EQ(mel_xxh64("a", 1, 0), 0xD24EC4F1A98C6E5BULL);
}

MEL_TEST(xxh64_three_bytes, .tags = "hash")
{
    MEL_ASSERT_EQ(mel_xxh64("abc", 3, 0), 0x44BC2CF5AD770999ULL);
}

MEL_TEST(xxh64_hello_world, .tags = "hash")
{
    MEL_ASSERT_EQ(mel_xxh64("Hello, World!", 13, 0), 0xC49AACF8080FE47FULL);
}

MEL_TEST(xxh64_with_seed, .tags = "hash")
{
    MEL_ASSERT_EQ(mel_xxh64("Hello, World!", 13, 42), 0xC2E0FE28B2512846ULL);
}

MEL_TEST(xxh64_32_boundary, .tags = "hash")
{
    char buf[32];
    memset(buf, 'a', 32);
    MEL_ASSERT_EQ(mel_xxh64(buf, 32, 0), 0x856E843298F99AD7ULL);
}

MEL_TEST(xxh64_33_crosses_boundary, .tags = "hash")
{
    char buf[33];
    memset(buf, 'a', 33);
    MEL_ASSERT_EQ(mel_xxh64(buf, 33, 0), 0x18F3FF0C21E3B24BULL);
}

MEL_TEST(xxh3_empty, .tags = "hash")
{
    MEL_ASSERT_EQ(mel_xxh3_64("", 0), 0x2D06800538D394C2ULL);
}

MEL_TEST(xxh3_1_byte, .tags = "hash")
{
    MEL_ASSERT_EQ(mel_xxh3_64("a", 1), 0xE6C632B61E964E1FULL);
}

MEL_TEST(xxh3_3_bytes, .tags = "hash")
{
    MEL_ASSERT_EQ(mel_xxh3_64("abc", 3), 0x78AF5F94892F3950ULL);
}

MEL_TEST(xxh3_8_bytes, .tags = "hash")
{
    char buf[8];
    memset(buf, 'a', 8);
    MEL_ASSERT_EQ(mel_xxh3_64(buf, 8), 0xC9DBC05573CD5D9AULL);
}

MEL_TEST(xxh3_16_bytes, .tags = "hash")
{
    char buf[16];
    memset(buf, 'a', 16);
    MEL_ASSERT_EQ(mel_xxh3_64(buf, 16), 0x13BA5039476CD10AULL);
}

MEL_TEST(xxh3_64_bytes, .tags = "hash")
{
    char buf[64];
    memset(buf, 'a', 64);
    MEL_ASSERT_EQ(mel_xxh3_64(buf, 64), 0x2DDDCEF07D26EE8CULL);
}

MEL_TEST(xxh3_128_bytes, .tags = "hash")
{
    char buf[128];
    memset(buf, 'a', 128);
    MEL_ASSERT_EQ(mel_xxh3_64(buf, 128), 0x7A22200AADC3D36CULL);
}

MEL_TEST(xxh3_240_bytes, .tags = "hash")
{
    char buf[240];
    memset(buf, 'a', 240);
    MEL_ASSERT_EQ(mel_xxh3_64(buf, 240), 0x993C46D96A01B5C6ULL);
}

MEL_TEST(xxh3_256_bytes, .tags = "hash")
{
    char buf[256];
    memset(buf, 'a', 256);
    MEL_ASSERT_EQ(mel_xxh3_64(buf, 256), 0x3FDB4FF1846C90F3ULL);
}

MEL_TEST(xxh3_1024_bytes, .tags = "hash")
{
    char buf[1024];
    memset(buf, 'a', 1024);
    MEL_ASSERT_EQ(mel_xxh3_64(buf, 1024), 0x4A5D6B09A9587A1CULL);
}

MEL_TEST(xxh3_seeded_short, .tags = "hash")
{
    MEL_ASSERT_EQ(mel_xxh3_64_seeded("abc", 3, 42), 0xD8438DEF21BBDCC3ULL);
}

MEL_TEST(xxh3_seeded_empty, .tags = "hash")
{
    MEL_ASSERT_EQ(mel_xxh3_64_seeded("", 0, 42), 0xB029411FF43D84D2ULL);
}

MEL_TEST(xxh3_seeded_long, .tags = "hash")
{
    char buf[256];
    memset(buf, 'a', 256);
    MEL_ASSERT_EQ(mel_xxh3_64_seeded(buf, 256, 42), 0x17DCFF7FF17E01CFULL);
}

MEL_TEST(xxh64_consistency, .tags = "hash")
{
    const char* data = "consistency check data";
    usize len = strlen(data);
    u64 h1 = mel_xxh64(data, len, 0);
    u64 h2 = mel_xxh64(data, len, 0);
    MEL_ASSERT_EQ(h1, h2);
}

MEL_TEST(xxh3_consistency, .tags = "hash")
{
    const char* data = "consistency check data";
    usize len = strlen(data);
    u64 h1 = mel_xxh3_64(data, len);
    u64 h2 = mel_xxh3_64(data, len);
    MEL_ASSERT_EQ(h1, h2);
}

MEL_TEST(xxh64_collision_check, .tags = "hash")
{
    u64 h1 = mel_xxh64("alpha", 5, 0);
    u64 h2 = mel_xxh64("beta", 4, 0);
    u64 h3 = mel_xxh64("gamma", 5, 0);
    u64 h4 = mel_xxh64("delta", 5, 0);
    MEL_ASSERT_NEQ(h1, h2);
    MEL_ASSERT_NEQ(h1, h3);
    MEL_ASSERT_NEQ(h1, h4);
    MEL_ASSERT_NEQ(h2, h3);
    MEL_ASSERT_NEQ(h2, h4);
    MEL_ASSERT_NEQ(h3, h4);
}

MEL_TEST(xxh3_collision_check, .tags = "hash")
{
    u64 h1 = mel_xxh3_64("alpha", 5);
    u64 h2 = mel_xxh3_64("beta", 4);
    u64 h3 = mel_xxh3_64("gamma", 5);
    u64 h4 = mel_xxh3_64("delta", 5);
    MEL_ASSERT_NEQ(h1, h2);
    MEL_ASSERT_NEQ(h1, h3);
    MEL_ASSERT_NEQ(h1, h4);
    MEL_ASSERT_NEQ(h2, h3);
    MEL_ASSERT_NEQ(h2, h4);
    MEL_ASSERT_NEQ(h3, h4);
}

MEL_TEST(xxh64_seed_differs, .tags = "hash")
{
    u64 h0 = mel_xxh64("test", 4, 0);
    u64 h1 = mel_xxh64("test", 4, 1);
    u64 h2 = mel_xxh64("test", 4, 0xFFFFFFFFFFFFFFFFULL);
    MEL_ASSERT_NEQ(h0, h1);
    MEL_ASSERT_NEQ(h0, h2);
    MEL_ASSERT_NEQ(h1, h2);
}

MEL_TEST(xxh3_seed_differs, .tags = "hash")
{
    u64 h0 = mel_xxh3_64_seeded("test", 4, 0);
    u64 h1 = mel_xxh3_64_seeded("test", 4, 1);
    u64 h2 = mel_xxh3_64_seeded("test", 4, 0xFFFFFFFFFFFFFFFFULL);
    MEL_ASSERT_NEQ(h0, h1);
    MEL_ASSERT_NEQ(h0, h2);
    MEL_ASSERT_NEQ(h1, h2);
}

MEL_TEST(xxh3_unseeded_matches_seed0, .tags = "hash")
{
    u64 h1 = mel_xxh3_64("test data here", 14);
    u64 h2 = mel_xxh3_64_seeded("test data here", 14, 0);
    MEL_ASSERT_EQ(h1, h2);

    char buf[300];
    memset(buf, 'x', 300);
    h1 = mel_xxh3_64(buf, 300);
    h2 = mel_xxh3_64_seeded(buf, 300, 0);
    MEL_ASSERT_EQ(h1, h2);

}
