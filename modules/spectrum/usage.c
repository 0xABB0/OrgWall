#include <spectrum/spectrum.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <stdio.h>

#define WINDOW 2048u
#define RATE   48000u

int main(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Spectrum* sp = mel_spectrum_create(alloc, WINDOW);
    u32           bins = mel_spectrum_bins(sp);

    f32 samples[WINDOW];
    for (u32 i = 0; i < WINDOW; i++)
        samples[i] = (i / 109u) & 1u ? 0.5f : -0.5f;

    f32 windowed[WINDOW];
    mel_spectrum_hann(windowed, samples, WINDOW);

    f32* mags = mel_alloc(alloc, bins * sizeof(f32));
    mel_spectrum_analyze(sp, windowed, mags);

    u32 peak = 0;
    for (u32 b = 1; b < bins; b++)
        if (mags[b] > mags[peak])
            peak = b;

    printf("peak %.1f Hz (%.3f)\n", mel_spectrum_bin_hz(peak, WINDOW, RATE), mags[peak]);

    mel_dealloc(alloc, mags);
    mel_spectrum_destroy(sp);
    return 0;
}
