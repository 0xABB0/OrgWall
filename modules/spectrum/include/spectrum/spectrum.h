#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Spectrum Mel_Spectrum;

Mel_Spectrum* mel_spectrum_create(const Mel_Alloc* a, u32 window_frames);
void          mel_spectrum_destroy(Mel_Spectrum* s);

u32 mel_spectrum_bins(const Mel_Spectrum* s);

void mel_spectrum_analyze(Mel_Spectrum* s, const f32* samples, f32* magnitudes);
void mel_spectrum_analyze_complex(Mel_Spectrum* s, const f32* samples, f32* re, f32* im);

void mel_spectrum_hann(f32* dst, const f32* src, u32 n);
void mel_spectrum_hamming(f32* dst, const f32* src, u32 n);
void mel_spectrum_blackman(f32* dst, const f32* src, u32 n);

f32 mel_spectrum_bin_hz(u32 bin, u32 window_frames, u32 samplerate);

#ifdef __cplusplus
}
#endif
