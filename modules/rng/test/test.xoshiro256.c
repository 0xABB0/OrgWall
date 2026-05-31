#include <rng/xoshiro256.h>
#include <test/test.h>

MEL_TEST(rng_xoshiro256, deterministic)
{
    Mel_Xoshiro256 a = mel_xoshiro256(0xC0FFEEu);
    Mel_Xoshiro256 b = mel_xoshiro256(0xC0FFEEu);
    for (int i = 0; i < 64; ++i)
        MEL_REQUIRE_EQ(mel_xoshiro256ss_next(&a), mel_xoshiro256ss_next(&b));
}

MEL_TEST(rng_xoshiro256, seed_avoids_zero_state)
{
    Mel_Xoshiro256 g = mel_xoshiro256(0u);
    MEL_EXPECT_NEQ(g.s[0] | g.s[1] | g.s[2] | g.s[3], 0ull);
}

MEL_TEST(rng_xoshiro256, jump_diverges_stream)
{
    Mel_Xoshiro256 base = mel_xoshiro256(123u);
    Mel_Xoshiro256 jumped = base;
    mel_xoshiro256_jump(&jumped);
    MEL_EXPECT_NEQ(mel_xoshiro256ss_next(&base), mel_xoshiro256ss_next(&jumped));
}

MEL_TEST(rng_xoshiro256, ss_and_pp_differ)
{
    Mel_Xoshiro256 a = mel_xoshiro256(99u);
    Mel_Xoshiro256 b = mel_xoshiro256(99u);
    MEL_EXPECT_NEQ(mel_xoshiro256ss_next(&a), mel_xoshiro256pp_next(&b));
}
