#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Audio_Status;

#define MEL_AUDIO_SEVERITY_MASK 0x3u
#define MEL_AUDIO_OK            0u
#define MEL_AUDIO_WARNED        1u
#define MEL_AUDIO_ERROR         2u

#define MEL_AUDIO_RESULT_DEVICE_DENIED   (1u << 2)
#define MEL_AUDIO_RESULT_FORMAT_UNGRANTED (1u << 3)
#define MEL_AUDIO_RESULT_NO_DEVICE       (1u << 4)
#define MEL_AUDIO_RESULT_DESTROYING      (1u << 5)
#define MEL_AUDIO_RESULT_STALE_VOICE     (1u << 6)
#define MEL_AUDIO_RESULT_BUDGET_CULLED   (1u << 7)

#define MEL_AUDIO_WARN_RATE_RESAMPLED    (1u << 8)
#define MEL_AUDIO_WARN_CHANNELS_REMIXED  (1u << 9)
#define MEL_AUDIO_WARN_RING_UNDERRUN     (1u << 10)
#define MEL_AUDIO_WARN_BLOCK_ADJUSTED    (1u << 11)

static inline bool mel_audio_failed(Mel_Audio_Status s) { return (s & MEL_AUDIO_SEVERITY_MASK) == MEL_AUDIO_ERROR; }
static inline bool mel_audio_warned(Mel_Audio_Status s) { return (s & MEL_AUDIO_SEVERITY_MASK) == MEL_AUDIO_WARNED; }

#ifdef __cplusplus
}
#endif
