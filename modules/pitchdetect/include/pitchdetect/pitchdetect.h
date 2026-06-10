#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <allocator/allocator.h>

typedef struct Mel_PitchDetector_Opt Mel_PitchDetector_Opt;

struct Mel_PitchDetector_Opt
{
    u32 sample_rate;
    u32 window_size;
    f64 min_hz;
    f64 max_hz;
    f32 threshold;
};

typedef struct Mel_PitchDetector Mel_PitchDetector;

struct Mel_PitchDetector
{
    const Mel_Alloc*      alloc;
    Mel_PitchDetector_Opt opt;
    u32                   tau_min;
    u32                   tau_max;
    f32*                  cmndf;
};

typedef struct Mel_Pitch_Estimate Mel_Pitch_Estimate;

struct Mel_Pitch_Estimate
{
    f64  frequency_hz;
    f32  clarity;
    bool voiced;
};

void               mel_pitch_detector_free(Mel_PitchDetector* d);
static inline void mel_pitch_detector_cleanup(Mel_PitchDetector* d) { mel_pitch_detector_free(d); }
#define Mel_PitchDetector_AUTO MEL_CLEANUP(mel_pitch_detector_cleanup) Mel_PitchDetector

MEL_NODISCARD Mel_PitchDetector mel_pitch_detector_make(const Mel_Alloc* alloc, Mel_PitchDetector_Opt opt);

MEL_NODISCARD Mel_Pitch_Estimate mel_pitch_detect(Mel_PitchDetector* d, const f32* samples, u32 count);
