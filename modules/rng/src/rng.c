#include <rng/rng.h>

#include <math.h>
#include <string.h>

u32 mel_rng_below_u32(Mel_Rng rng, u32 bound)
{
    if (bound == 0)
        return 0;
    u32 x = mel_rng_u32(rng);
    u64 m = (u64)x * (u64)bound;
    u32 lo = (u32)m;
    if (lo < bound)
    {
        u32 threshold = (0u - bound) % bound;
        while (lo < threshold)
        {
            x = mel_rng_u32(rng);
            m = (u64)x * (u64)bound;
            lo = (u32)m;
        }
    }
    return (u32)(m >> 32);
}

u64 mel_rng_below_u64(Mel_Rng rng, u64 bound)
{
    if (bound == 0)
        return 0;
    u64 threshold = (0ull - bound) % bound;
    for (;;)
    {
        u64 r = mel_rng_next(rng);
        if (r >= threshold)
            return r % bound;
    }
}

usize mel_rng_index(Mel_Rng rng, usize count) { return (usize)mel_rng_below_u64(rng, (u64)count); }

i32 mel_rng_range_i32(Mel_Rng rng, i32 lo, i32 hi)
{
    if (hi <= lo)
        return lo;
    u32 span = (u32)((i64)hi - (i64)lo) + 1u;
    return lo + (i32)mel_rng_below_u32(rng, span);
}

i64 mel_rng_range_i64(Mel_Rng rng, i64 lo, i64 hi)
{
    if (hi <= lo)
        return lo;
    u64 span = (u64)(hi - lo) + 1u;
    return lo + (i64)mel_rng_below_u64(rng, span);
}

f32 mel_rng_range_f32(Mel_Rng rng, f32 lo, f32 hi) { return lo + (hi - lo) * mel_rng_f32(rng); }

f64 mel_rng_range_f64(Mel_Rng rng, f64 lo, f64 hi) { return lo + (hi - lo) * mel_rng_f64(rng); }

bool mel_rng_chance(Mel_Rng rng, f64 probability)
{
    if (probability <= 0.0)
        return false;
    if (probability >= 1.0)
        return true;
    return mel_rng_f64(rng) < probability;
}

f64 mel_rng_normal(Mel_Rng rng)
{
    f64 u, v, s;
    do
    {
        u = 2.0 * mel_rng_f64(rng) - 1.0;
        v = 2.0 * mel_rng_f64(rng) - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    return u * sqrt(-2.0 * log(s) / s);
}

f64 mel_rng_normal_about(Mel_Rng rng, f64 mean, f64 stddev) { return mean + stddev * mel_rng_normal(rng); }

f64 mel_rng_exponential(Mel_Rng rng, f64 rate)
{
    f64 u = 1.0 - mel_rng_f64(rng);
    return -log(u) / rate;
}

void mel_rng_fill(Mel_Rng rng, void* dst, usize bytes)
{
    u8*   p = (u8*)dst;
    usize n = bytes;
    while (n >= 8)
    {
        u64 word = mel_rng_next(rng);
        memcpy(p, &word, 8);
        p += 8;
        n -= 8;
    }
    if (n)
    {
        u64 word = mel_rng_next(rng);
        memcpy(p, &word, n);
    }
}

void mel_rng_shuffle(Mel_Rng rng, void* base, usize count, usize size)
{
    u8* a = (u8*)base;
    for (usize i = count; i > 1; --i)
    {
        usize j = (usize)mel_rng_below_u64(rng, (u64)i);
        usize x = i - 1;
        if (j == x)
            continue;
        u8* pa = a + x * size;
        u8* pb = a + j * size;
        for (usize k = 0; k < size; ++k)
        {
            u8 t = pa[k];
            pa[k] = pb[k];
            pb[k] = t;
        }
    }
}
