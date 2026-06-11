#include <spectrum/spectrum.h>

#include <core/types.h>
#include <allocator/allocator.h>

#include <math.h>

#define MEL_SPECTRUM__TAU 6.28318530717958647692

struct Mel_Spectrum
{
    u32              window;
    u32              half;
    u32              bins;
    u32*             rev;
    f32*             tw_re;
    f32*             tw_im;
    f32*             post_re;
    f32*             post_im;
    f32*             zre;
    f32*             zim;
    void*            slab;
    const Mel_Alloc* alloc;
};

Mel_Spectrum* mel_spectrum_create(const Mel_Alloc* a, u32 window_frames)
{
    assert(a != NULL);
    assert(window_frames >= 2u);
    assert((window_frames & (window_frames - 1u)) == 0u);

    Mel_Spectrum* s = mel_alloc(a, sizeof(*s));
    if (s == NULL)
        return NULL;

    u32 half = window_frames / 2u;
    u32 bins = half + 1u;
    u32 quarter = half / 2u;

    usize tw_count = (usize)quarter + 1u;
    usize rev_bytes = sizeof(u32) * (usize)half;
    usize f32_count = tw_count * 2u + (usize)bins * 2u + (usize)half * 2u;

    void* slab = mel_calloc(a, rev_bytes + sizeof(f32) * f32_count);
    if (slab == NULL)
    {
        mel_dealloc(a, s);
        return NULL;
    }

    s->window = window_frames;
    s->half = half;
    s->bins = bins;
    s->rev = slab;
    f32* p = (f32*)(s->rev + half);
    s->tw_re = p;
    p += tw_count;
    s->tw_im = p;
    p += tw_count;
    s->post_re = p;
    p += bins;
    s->post_im = p;
    p += bins;
    s->zre = p;
    p += half;
    s->zim = p;
    s->slab = slab;
    s->alloc = a;

    u32 log2_half = 0;
    while ((1u << log2_half) < half)
        log2_half++;
    for (u32 i = 0; i < half; i++)
    {
        u32 r = 0;
        for (u32 b = 0; b < log2_half; b++)
            r |= ((i >> b) & 1u) << (log2_half - 1u - b);
        s->rev[i] = r;
    }

    for (u32 k = 0; k <= quarter; k++)
    {
        f64 ang = -MEL_SPECTRUM__TAU * (f64)k / (f64)half;
        s->tw_re[k] = (f32)cos(ang);
        s->tw_im[k] = (f32)sin(ang);
    }

    for (u32 k = 0; k < bins; k++)
    {
        f64 ang = -MEL_SPECTRUM__TAU * (f64)k / (f64)window_frames;
        s->post_re[k] = (f32)cos(ang);
        s->post_im[k] = (f32)sin(ang);
    }

    return s;
}

void mel_spectrum_destroy(Mel_Spectrum* s)
{
    if (s == NULL)
        return;
    const Mel_Alloc* a = s->alloc;
    mel_dealloc(a, s->slab);
    mel_dealloc(a, s);
}

u32 mel_spectrum_bins(const Mel_Spectrum* s)
{
    assert(s != NULL);
    return s->bins;
}

static void mel_spectrum__fft(Mel_Spectrum* s)
{
    u32  n = s->half;
    u32* rev = s->rev;
    f32* re = s->zre;
    f32* im = s->zim;

    for (u32 i = 0; i < n; i++)
    {
        u32 j = rev[i];
        if (j > i)
        {
            f32 tr = re[i];
            re[i] = re[j];
            re[j] = tr;
            f32 ti = im[i];
            im[i] = im[j];
            im[j] = ti;
        }
    }

    for (u32 len = 2; len <= n; len <<= 1)
    {
        u32 step = n / len;
        u32 mid = len / 2u;
        for (u32 base = 0; base < n; base += len)
        {
            for (u32 j = 0; j < mid; j++)
            {
                u32 k = j * step;
                f32 wr = s->tw_re[k];
                f32 wi = s->tw_im[k];
                u32 ia = base + j;
                u32 ib = ia + mid;
                f32 xr = re[ib] * wr - im[ib] * wi;
                f32 xi = re[ib] * wi + im[ib] * wr;
                re[ib] = re[ia] - xr;
                im[ib] = im[ia] - xi;
                re[ia] += xr;
                im[ia] += xi;
            }
        }
    }
}

static void mel_spectrum__pack_fft(Mel_Spectrum* s, const f32* samples)
{
    for (u32 j = 0; j < s->half; j++)
    {
        s->zre[j] = samples[2u * j + 0u];
        s->zim[j] = samples[2u * j + 1u];
    }
    mel_spectrum__fft(s);
}

static void mel_spectrum__bin(const Mel_Spectrum* s, u32 k, f32* out_re, f32* out_im)
{
    u32 half = s->half;
    u32 ki = k < half ? k : 0u;
    u32 mi = (half - k) % half;

    f32 ar = s->zre[ki];
    f32 ai = s->zim[ki];
    f32 br = s->zre[mi];
    f32 bi = s->zim[mi];

    f32 fe_re = 0.5f * (ar + br);
    f32 fe_im = 0.5f * (ai - bi);
    f32 fo_re = 0.5f * (ai + bi);
    f32 fo_im = -0.5f * (ar - br);

    f32 wr = s->post_re[k];
    f32 wi = s->post_im[k];

    *out_re = fe_re + wr * fo_re - wi * fo_im;
    *out_im = fe_im + wr * fo_im + wi * fo_re;
}

void mel_spectrum_analyze(Mel_Spectrum* s, const f32* samples, f32* magnitudes)
{
    assert(s != NULL);
    assert(samples != NULL);
    assert(magnitudes != NULL);
    assert(samples != magnitudes);

    mel_spectrum__pack_fft(s, samples);

    for (u32 k = 0; k < s->bins; k++)
    {
        f32 xr;
        f32 xi;
        mel_spectrum__bin(s, k, &xr, &xi);
        magnitudes[k] = sqrtf(xr * xr + xi * xi);
    }
}

void mel_spectrum_analyze_complex(Mel_Spectrum* s, const f32* samples, f32* re, f32* im)
{
    assert(s != NULL);
    assert(samples != NULL);
    assert(re != NULL);
    assert(im != NULL);
    assert(samples != re);
    assert(samples != im);
    assert(re != im);

    mel_spectrum__pack_fft(s, samples);

    for (u32 k = 0; k < s->bins; k++)
        mel_spectrum__bin(s, k, re + k, im + k);
}

void mel_spectrum_hann(f32* dst, const f32* src, u32 n)
{
    assert(dst != NULL);
    assert(src != NULL);

    if (n == 0u)
        return;
    if (n == 1u)
    {
        dst[0] = src[0];
        return;
    }

    f64 step = MEL_SPECTRUM__TAU / (f64)(n - 1u);
    for (u32 i = 0; i < n; i++)
        dst[i] = src[i] * (f32)(0.5 - 0.5 * cos(step * (f64)i));
}

void mel_spectrum_hamming(f32* dst, const f32* src, u32 n)
{
    assert(dst != NULL);
    assert(src != NULL);

    if (n == 0u)
        return;
    if (n == 1u)
    {
        dst[0] = src[0];
        return;
    }

    f64 step = MEL_SPECTRUM__TAU / (f64)(n - 1u);
    for (u32 i = 0; i < n; i++)
        dst[i] = src[i] * (f32)(0.54 - 0.46 * cos(step * (f64)i));
}

void mel_spectrum_blackman(f32* dst, const f32* src, u32 n)
{
    assert(dst != NULL);
    assert(src != NULL);

    if (n == 0u)
        return;
    if (n == 1u)
    {
        dst[0] = src[0];
        return;
    }

    f64 step = MEL_SPECTRUM__TAU / (f64)(n - 1u);
    for (u32 i = 0; i < n; i++)
    {
        f64 ang = step * (f64)i;
        dst[i] = src[i] * (f32)(0.42 - 0.5 * cos(ang) + 0.08 * cos(2.0 * ang));
    }
}

f32 mel_spectrum_bin_hz(u32 bin, u32 window_frames, u32 samplerate)
{
    assert(window_frames > 0u);
    return (f32)((f64)bin * (f64)samplerate / (f64)window_frames);
}
