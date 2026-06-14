#pragma once

#include <mpfr.h>

#include <core/compiler.h>
#include <core/types.h>
#include <frequency/cent.h>
#include <musictuning/tuning.h>

#include "pitch.h"

typedef struct Mel_Interval Mel_Interval;

struct Mel_Interval
{
    const Mel_Tuning* tuning;
    i64               ref_index;
    i64               diff;
};

#ifdef __cplusplus
extern "C"
{
#endif

MEL_NODISCARD Mel_Interval mel_interval_make(const Mel_Tuning* tuning, i64 ref_index, i64 diff);

MEL_NODISCARD Mel_Interval mel_interval_from_pitches(Mel_Pitch source, Mel_Pitch target);

MEL_NODISCARD Mel_Interval mel_interval_abs(Mel_Interval i);

MEL_NODISCARD i64 mel_interval_abs_diff(Mel_Interval i);

MEL_NODISCARD Mel_Interval mel_interval_negate(Mel_Interval i);

MEL_NODISCARD Mel_Interval mel_interval_add(Mel_Interval a, Mel_Interval b);

MEL_NODISCARD Mel_Interval mel_interval_mul(Mel_Interval i, i64 scalar);

void mel_interval_ratio(mpfr_ptr out, Mel_Interval i);

MEL_NODISCARD Mel_Cent mel_interval_cents(Mel_Interval i);

MEL_NODISCARD u8 mel_interval_eq(Mel_Interval a, Mel_Interval b);
MEL_NODISCARD u8 mel_interval_cmp(Mel_Interval a, Mel_Interval b);

#ifdef __cplusplus
}
#endif
