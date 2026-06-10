#include <pitchdetect/pitchdetect.h>

#include <assert.h>
#include <math.h>

void mel_pitch_detector_free(Mel_PitchDetector* d)
{
    if (!d || !d->cmndf)
        return;
    mel_dealloc(d->alloc, d->cmndf);
    d->cmndf = NULL;
}

Mel_PitchDetector mel_pitch_detector_make(const Mel_Alloc* alloc, Mel_PitchDetector_Opt opt)
{
    assert(alloc);
    assert(opt.sample_rate > 0);
    assert(opt.min_hz > 0.0 && opt.min_hz < opt.max_hz);
    assert(opt.max_hz < (f64)opt.sample_rate / 2.0);
    assert(opt.threshold > 0.0f && opt.threshold < 1.0f);

    Mel_PitchDetector d;
    d.alloc = alloc;
    d.opt = opt;
    d.tau_min = (u32)floor((f64)opt.sample_rate / opt.max_hz);
    d.tau_max = (u32)ceil((f64)opt.sample_rate / opt.min_hz);
    assert(d.tau_min >= 2);
    assert(opt.window_size >= 2 * d.tau_max);

    d.cmndf = mel_alloc_array(alloc, f32, d.tau_max + 1);
    return d;
}

Mel_Pitch_Estimate mel_pitch_detect(Mel_PitchDetector* d, const f32* samples, u32 count)
{
    assert(samples);
    assert(count == d->opt.window_size);

    u32  w = d->opt.window_size / 2;
    f32* cmndf = d->cmndf;

    cmndf[0] = 1.0f;
    f64 running_sum = 0.0;
    for (u32 tau = 1; tau <= d->tau_max; tau++)
    {
        f64 diff = 0.0;
        for (u32 i = 0; i < w; i++)
        {
            f64 delta = (f64)samples[i] - (f64)samples[i + tau];
            diff += delta * delta;
        }
        running_sum += diff;
        cmndf[tau] = running_sum > 0.0 ? (f32)(diff * (f64)tau / running_sum) : 1.0f;
    }

    u32 best_tau = 0;
    for (u32 tau = d->tau_min; tau <= d->tau_max; tau++)
    {
        if (cmndf[tau] < d->opt.threshold)
        {
            while (tau + 1 <= d->tau_max && cmndf[tau + 1] < cmndf[tau])
                tau++;
            best_tau = tau;
            break;
        }
    }

    bool voiced = best_tau != 0;
    if (!voiced)
    {
        best_tau = d->tau_min;
        for (u32 tau = d->tau_min + 1; tau <= d->tau_max; tau++)
            if (cmndf[tau] < cmndf[best_tau])
                best_tau = tau;
    }

    f64 refined = (f64)best_tau;
    if (best_tau > 1 && best_tau < d->tau_max)
    {
        f64 prev = cmndf[best_tau - 1];
        f64 here = cmndf[best_tau];
        f64 next = cmndf[best_tau + 1];
        f64 denom = prev - 2.0 * here + next;
        if (denom > 0.0)
            refined += 0.5 * (prev - next) / denom;
    }

    f32 clarity = 1.0f - cmndf[best_tau];
    if (clarity < 0.0f)
        clarity = 0.0f;
    if (clarity > 1.0f)
        clarity = 1.0f;

    Mel_Pitch_Estimate e;
    e.voiced = voiced;
    e.clarity = clarity;
    e.frequency_hz = voiced ? (f64)d->opt.sample_rate / refined : 0.0;
    return e;
}
