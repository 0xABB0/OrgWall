#include <rng/pcg32.h>
#include <test/test.h>

MEL_TEST(rng_pcg32, reference_vector)
{
    Mel_Pcg32 g = mel_pcg32(42u, 54u);
    MEL_REQUIRE_EQ(mel_pcg32_next(&g), 0xa15c02b7u);
    MEL_REQUIRE_EQ(mel_pcg32_next(&g), 0x7b47f409u);
    MEL_REQUIRE_EQ(mel_pcg32_next(&g), 0xba1d3330u);
    MEL_REQUIRE_EQ(mel_pcg32_next(&g), 0x83d2f293u);
    MEL_REQUIRE_EQ(mel_pcg32_next(&g), 0xbfa4784bu);
    MEL_REQUIRE_EQ(mel_pcg32_next(&g), 0xcbed606eu);
}

MEL_TEST(rng_pcg32, streams_are_distinct)
{
    Mel_Pcg32 a = mel_pcg32(42u, 1u);
    Mel_Pcg32 b = mel_pcg32(42u, 2u);
    MEL_EXPECT_NEQ(mel_pcg32_next(&a), mel_pcg32_next(&b));
}

MEL_TEST(rng_pcg32, deterministic)
{
    Mel_Pcg32 a = mel_pcg32(7u, 7u);
    Mel_Pcg32 b = mel_pcg32(7u, 7u);
    for (int i = 0; i < 64; ++i)
        MEL_REQUIRE_EQ(mel_pcg32_next(&a), mel_pcg32_next(&b));
}
