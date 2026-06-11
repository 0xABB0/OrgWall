#include <test/test.h>

#include <spectrum/spectrum.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/types.h>

#include <math.h>

#define TAU 6.28318530717958647692

MEL_TEST(spectrum, bins_is_half_plus_one)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Spectrum*    s = mel_spectrum_create(a, 8u);
    MEL_REQUIRE_NOT_NULL(s);
    MEL_EXPECT_EQ(mel_spectrum_bins(s), 5u);
    mel_spectrum_destroy(s);

    s = mel_spectrum_create(a, 2048u);
    MEL_REQUIRE_NOT_NULL(s);
    MEL_EXPECT_EQ(mel_spectrum_bins(s), 1025u);
    mel_spectrum_destroy(s);
}

MEL_TEST(spectrum, dc_lands_in_bin_zero)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Spectrum*    s = mel_spectrum_create(a, 32u);
    MEL_REQUIRE_NOT_NULL(s);

    f32 samples[32];
    for (u32 i = 0; i < 32u; i++)
        samples[i] = 1.0f;

    f32 mags[17];
    mel_spectrum_analyze(s, samples, mags);

    MEL_EXPECT_FLOAT_EQ(mags[0], 32.0f, 1e-3f);
    for (u32 k = 1; k < 17u; k++)
        MEL_EXPECT_FLOAT_EQ(mags[k], 0.0f, 1e-3f);

    mel_spectrum_destroy(s);
}

MEL_TEST(spectrum, pure_cosine_peaks_at_its_bin)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Spectrum*    s = mel_spectrum_create(a, 64u);
    MEL_REQUIRE_NOT_NULL(s);

    const u32 target = 5u;
    f32       samples[64];
    for (u32 i = 0; i < 64u; i++)
        samples[i] = 0.8f * (f32)cos(TAU * (f64)target * (f64)i / 64.0);

    f32 mags[33];
    mel_spectrum_analyze(s, samples, mags);

    MEL_EXPECT_FLOAT_EQ(mags[target], 0.8f * 32.0f, 1e-2f);
    for (u32 k = 0; k < 33u; k++)
        if (k != target)
            MEL_EXPECT_FLOAT_EQ(mags[k], 0.0f, 1e-2f);

    mel_spectrum_destroy(s);
}

MEL_TEST(spectrum, nyquist_lands_in_last_bin)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Spectrum*    s = mel_spectrum_create(a, 16u);
    MEL_REQUIRE_NOT_NULL(s);

    f32 samples[16];
    for (u32 i = 0; i < 16u; i++)
        samples[i] = (i & 1u) ? -1.0f : 1.0f;

    f32 mags[9];
    mel_spectrum_analyze(s, samples, mags);

    MEL_EXPECT_FLOAT_EQ(mags[8], 16.0f, 1e-3f);
    for (u32 k = 0; k < 8u; k++)
        MEL_EXPECT_FLOAT_EQ(mags[k], 0.0f, 1e-3f);

    mel_spectrum_destroy(s);
}

MEL_TEST(spectrum, matches_naive_dft)
{
    const Mel_Alloc* a = mel_alloc_heap();
    const u32        n = 64u;
    Mel_Spectrum*    s = mel_spectrum_create(a, n);
    MEL_REQUIRE_NOT_NULL(s);

    f32 samples[64];
    u32 seed = 0x12345678u;
    for (u32 i = 0; i < n; i++)
    {
        seed = seed * 1664525u + 1013904223u;
        samples[i] = ((f32)(seed >> 8) / (f32)(1u << 24)) * 2.0f - 1.0f;
    }

    f32 re[33];
    f32 im[33];
    mel_spectrum_analyze_complex(s, samples, re, im);

    f32 mags[33];
    mel_spectrum_analyze(s, samples, mags);

    for (u32 k = 0; k <= n / 2u; k++)
    {
        f64 sum_re = 0.0;
        f64 sum_im = 0.0;
        for (u32 i = 0; i < n; i++)
        {
            f64 ang = -TAU * (f64)k * (f64)i / (f64)n;
            sum_re += (f64)samples[i] * cos(ang);
            sum_im += (f64)samples[i] * sin(ang);
        }
        MEL_EXPECT_FLOAT_EQ(re[k], (f32)sum_re, 1e-3f);
        MEL_EXPECT_FLOAT_EQ(im[k], (f32)sum_im, 1e-3f);
        MEL_EXPECT_FLOAT_EQ(mags[k], (f32)sqrt(sum_re * sum_re + sum_im * sum_im), 1e-3f);
    }

    mel_spectrum_destroy(s);
}

MEL_TEST(spectrum, smallest_window_is_sum_and_difference)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Spectrum*    s = mel_spectrum_create(a, 2u);
    MEL_REQUIRE_NOT_NULL(s);
    MEL_EXPECT_EQ(mel_spectrum_bins(s), 2u);

    f32 samples[2] = { 3.0f, 1.0f };
    f32 mags[2];
    mel_spectrum_analyze(s, samples, mags);

    MEL_EXPECT_FLOAT_EQ(mags[0], 4.0f, 1e-6f);
    MEL_EXPECT_FLOAT_EQ(mags[1], 2.0f, 1e-6f);

    mel_spectrum_destroy(s);
}

MEL_TEST(spectrum, sine_phase_is_negative_imaginary)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Spectrum*    s = mel_spectrum_create(a, 32u);
    MEL_REQUIRE_NOT_NULL(s);

    const u32 target = 3u;
    f32       samples[32];
    for (u32 i = 0; i < 32u; i++)
        samples[i] = (f32)sin(TAU * (f64)target * (f64)i / 32.0);

    f32 re[17];
    f32 im[17];
    mel_spectrum_analyze_complex(s, samples, re, im);

    MEL_EXPECT_FLOAT_EQ(re[target], 0.0f, 1e-3f);
    MEL_EXPECT_FLOAT_EQ(im[target], -16.0f, 1e-3f);

    mel_spectrum_destroy(s);
}

MEL_TEST(spectrum, hann_window_shape)
{
    f32 src[9];
    for (u32 i = 0; i < 9u; i++)
        src[i] = 2.0f;

    f32 dst[9];
    mel_spectrum_hann(dst, src, 9u);

    MEL_EXPECT_FLOAT_EQ(dst[0], 0.0f, 1e-6f);
    MEL_EXPECT_FLOAT_EQ(dst[4], 2.0f, 1e-6f);
    MEL_EXPECT_FLOAT_EQ(dst[8], 0.0f, 1e-6f);
    MEL_EXPECT_FLOAT_EQ(dst[2], 1.0f, 1e-5f);
}

MEL_TEST(spectrum, hamming_window_endpoints)
{
    f32 src[9];
    for (u32 i = 0; i < 9u; i++)
        src[i] = 1.0f;

    f32 dst[9];
    mel_spectrum_hamming(dst, src, 9u);

    MEL_EXPECT_FLOAT_EQ(dst[0], 0.08f, 1e-5f);
    MEL_EXPECT_FLOAT_EQ(dst[4], 1.0f, 1e-5f);
    MEL_EXPECT_FLOAT_EQ(dst[8], 0.08f, 1e-5f);
}

MEL_TEST(spectrum, blackman_window_endpoints)
{
    f32 src[9];
    for (u32 i = 0; i < 9u; i++)
        src[i] = 1.0f;

    f32 dst[9];
    mel_spectrum_blackman(dst, src, 9u);

    MEL_EXPECT_FLOAT_EQ(dst[0], 0.0f, 1e-5f);
    MEL_EXPECT_FLOAT_EQ(dst[4], 1.0f, 1e-5f);
    MEL_EXPECT_FLOAT_EQ(dst[8], 0.0f, 1e-5f);
}

MEL_TEST(spectrum, bin_hz_mapping)
{
    MEL_EXPECT_FLOAT_EQ(mel_spectrum_bin_hz(0u, 2048u, 48000u), 0.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(mel_spectrum_bin_hz(1u, 2048u, 48000u), 23.4375f, 1e-4f);
    MEL_EXPECT_FLOAT_EQ(mel_spectrum_bin_hz(1024u, 2048u, 48000u), 24000.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(mel_spectrum_bin_hz(441u, 1024u, 44100u), 18992.285f, 1e-2f);
}
