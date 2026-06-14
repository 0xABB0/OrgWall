#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <musictuning/tuning.h>

typedef struct Mel_Pitch Mel_Pitch;

struct Mel_Pitch
{
    const Mel_Tuning* tuning;
    i64               index;
};

#ifdef __cplusplus
extern "C"
{
#endif

MEL_NODISCARD Mel_Pitch mel_pitch_make(const Mel_Tuning* tuning, i64 index);

MEL_NODISCARD Mel_Hz mel_pitch_frequency(Mel_Pitch p);

MEL_NODISCARD Mel_Pitch mel_pitch_transpose(Mel_Pitch p, i64 diff);

MEL_NODISCARD Mel_Pitch mel_pitch_retune(Mel_Pitch p, const Mel_Tuning* target_tuning);

MEL_NODISCARD i64 mel_pitch_pc_index(Mel_Pitch p);
MEL_NODISCARD i64 mel_pitch_bi_index(Mel_Pitch p);

MEL_NODISCARD Mel_Pitch mel_pitch_transpose_bi(Mel_Pitch p, i64 bi_diff);

MEL_NODISCARD Mel_Pitch mel_pitch_pcs_normalized(Mel_Pitch p);

MEL_NODISCARD u8 mel_pitch_is_equivalent(Mel_Pitch a, Mel_Pitch b);

MEL_NODISCARD i64 mel_pitch_generator_distance(Mel_Pitch pitch, Mel_Pitch generator);

MEL_NODISCARD u8 mel_pitch_eq(Mel_Pitch a, Mel_Pitch b);
MEL_NODISCARD u8 mel_pitch_cmp(Mel_Pitch a, Mel_Pitch b);

#ifdef __cplusplus
}
#endif

MEL_NODISCARD static inline int mel_pitch_cmp_int(Mel_Pitch a, Mel_Pitch b)
{
    u8 c = mel_pitch_cmp(a, b);
    if (c == 0)
        return -1;
    if (c == 1)
        return 0;
    return 1;
}
