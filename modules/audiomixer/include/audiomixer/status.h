#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Mixer_Status;

#define MEL_MIXER_SEVERITY_MASK 0x3u
#define MEL_MIXER_OK            0u
#define MEL_MIXER_WARNED        1u
#define MEL_MIXER_ERROR         2u

#define MEL_MIXER_RESULT_DEVICE_DENIED   (1u << 2)
#define MEL_MIXER_RESULT_FORMAT_UNGRANTED (1u << 3)
#define MEL_MIXER_RESULT_NO_DEVICE       (1u << 4)
#define MEL_MIXER_RESULT_DESTROYING      (1u << 5)
#define MEL_MIXER_RESULT_STALE_VOICE     (1u << 6)
#define MEL_MIXER_RESULT_BUDGET_CULLED   (1u << 7)

#define MEL_MIXER_RESULT_DEVICE_LOST     (1u << 12)
#define MEL_MIXER_RESULT_INTERRUPTED     (1u << 13)

#define MEL_MIXER_WARN_RATE_RESAMPLED    (1u << 8)
#define MEL_MIXER_WARN_CHANNELS_REMIXED  (1u << 9)
#define MEL_MIXER_WARN_RING_UNDERRUN     (1u << 10)
#define MEL_MIXER_WARN_BLOCK_ADJUSTED    (1u << 11)

static inline bool mel_mixer_failed(Mel_Mixer_Status s) { return (s & MEL_MIXER_SEVERITY_MASK) == MEL_MIXER_ERROR; }
static inline bool mel_mixer_warned(Mel_Mixer_Status s) { return (s & MEL_MIXER_SEVERITY_MASK) == MEL_MIXER_WARNED; }

#ifdef __cplusplus
}
#endif
