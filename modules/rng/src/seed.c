#include <rng/entropy.h>

u64 mel_rng_entropy_u64(void)
{
    u64  v = 0;
    bool ok = mel_rng_entropy(&v, sizeof v);
    assert(ok && "mel_rng_entropy failed");
    (void)ok;
    return v;
}

Mel_Pcg32 mel_pcg32_seeded(void)
{
    u64  words[2] = { 0, 0 };
    bool ok = mel_rng_entropy(words, sizeof words);
    assert(ok && "mel_rng_entropy failed");
    (void)ok;
    return mel_pcg32(words[0], words[1]);
}

Mel_Xoshiro256 mel_xoshiro256_seeded(void) { return mel_xoshiro256(mel_rng_entropy_u64()); }
