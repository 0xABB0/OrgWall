#include <rng/xoshiro256.h>

static void mel__xoshiro256_jump_by(Mel_Xoshiro256* g, const u64 poly[4])
{
    u64 j[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 4; ++i)
    {
        for (int b = 0; b < 64; ++b)
        {
            if (poly[i] & (1ull << b))
            {
                j[0] ^= g->s[0];
                j[1] ^= g->s[1];
                j[2] ^= g->s[2];
                j[3] ^= g->s[3];
            }
            mel__xoshiro256_advance(g->s);
        }
    }
    g->s[0] = j[0];
    g->s[1] = j[1];
    g->s[2] = j[2];
    g->s[3] = j[3];
}

void mel_xoshiro256_jump(Mel_Xoshiro256* g)
{
    static const u64 poly[4] = { 0x180ec6d33cfd0abaull, 0xd5a61266f0c9392cull, 0xa9582618e03fc9aaull, 0x39abdc4529b1661cull };
    mel__xoshiro256_jump_by(g, poly);
}

void mel_xoshiro256_long_jump(Mel_Xoshiro256* g)
{
    static const u64 poly[4] = { 0x76e15d3efefdcbbfull, 0xc5004e441c522fb3ull, 0x77710069854ee241ull, 0x39109bb02acbe635ull };
    mel__xoshiro256_jump_by(g, poly);
}
