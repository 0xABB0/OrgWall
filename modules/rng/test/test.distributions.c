#include <rng/pcg32.h>
#include <rng/rng.h>
#include <test/test.h>

MEL_TEST(rng_dist, below_respects_bound)
{
    Mel_Pcg32 g = mel_pcg32(1u, 1u);
    Mel_Rng   rng = mel_pcg32_rng(&g);
    for (int i = 0; i < 4096; ++i)
    {
        MEL_REQUIRE_LT(mel_rng_below_u32(rng, 7u), 7u);
        MEL_REQUIRE_LT(mel_rng_below_u64(rng, 1000ull), 1000ull);
    }
    MEL_EXPECT_EQ(mel_rng_below_u32(rng, 1u), 0u);
    MEL_EXPECT_EQ(mel_rng_below_u64(rng, 1ull), 0ull);
}

MEL_TEST(rng_dist, range_is_inclusive)
{
    Mel_Pcg32 g = mel_pcg32(2u, 2u);
    Mel_Rng   rng = mel_pcg32_rng(&g);
    bool      saw_lo = false, saw_hi = false;
    for (int i = 0; i < 100000; ++i)
    {
        i32 v = mel_rng_range_i32(rng, -3, 3);
        MEL_REQUIRE_GE(v, -3);
        MEL_REQUIRE_LE(v, 3);
        if (v == -3)
            saw_lo = true;
        if (v == 3)
            saw_hi = true;
    }
    MEL_EXPECT(saw_lo);
    MEL_EXPECT(saw_hi);
}

MEL_TEST(rng_dist, unit_floats_in_range)
{
    Mel_Pcg32 g = mel_pcg32(3u, 3u);
    Mel_Rng   rng = mel_pcg32_rng(&g);
    for (int i = 0; i < 100000; ++i)
    {
        f32 a = mel_rng_f32(rng);
        f64 b = mel_rng_f64(rng);
        MEL_REQUIRE_GE(a, 0.0f);
        MEL_REQUIRE_LT(a, 1.0f);
        MEL_REQUIRE_GE(b, 0.0);
        MEL_REQUIRE_LT(b, 1.0);
    }
}

MEL_TEST(rng_dist, shuffle_preserves_multiset)
{
    Mel_Pcg32 g = mel_pcg32(4u, 4u);
    Mel_Rng   rng = mel_pcg32_rng(&g);
    i32       a[16];
    for (i32 i = 0; i < 16; ++i)
        a[i] = i;
    mel_rng_shuffle(rng, a, 16, sizeof a[0]);
    i32 seen = 0;
    for (i32 i = 0; i < 16; ++i)
        for (i32 j = 0; j < 16; ++j)
            if (a[j] == i)
                seen++;
    MEL_EXPECT_EQ(seen, 16);
}

MEL_TEST(rng_dist, fill_touches_exact_bytes)
{
    Mel_Pcg32 g = mel_pcg32(5u, 5u);
    Mel_Rng   rng = mel_pcg32_rng(&g);
    u8        buf[16];
    for (int i = 0; i < 16; ++i)
        buf[i] = 0xAB;
    mel_rng_fill(rng, buf, 13);
    MEL_EXPECT_EQ(buf[13], 0xAB);
    MEL_EXPECT_EQ(buf[14], 0xAB);
    MEL_EXPECT_EQ(buf[15], 0xAB);
}
