#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <allocator/allocator.fwd.h>
#include <audioout/audioout.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_AudioPlayback_Status;

#define MEL_AUDIOPLAYBACK_SEVERITY_MASK 0x3u
#define MEL_AUDIOPLAYBACK_OK            0u
#define MEL_AUDIOPLAYBACK_WARNED        1u
#define MEL_AUDIOPLAYBACK_ERROR         2u

#define MEL_AUDIOPLAYBACK_RESULT_NO_DEVICE   (1u << 2)
#define MEL_AUDIOPLAYBACK_RESULT_UNSUPPORTED (1u << 3)
#define MEL_AUDIOPLAYBACK_RESULT_BUSY        (1u << 4)
#define MEL_AUDIOPLAYBACK_RESULT_LOST        (1u << 5)

#define MEL_AUDIOPLAYBACK_WARN_CONVERTED         (1u << 6)
#define MEL_AUDIOPLAYBACK_WARN_UNDERRUN          (1u << 7)
#define MEL_AUDIOPLAYBACK_WARN_EXCLUSIVE_DROPPED (1u << 8)

static inline bool mel_audioplayback_status_failed(Mel_AudioPlayback_Status s) { return (s & MEL_AUDIOPLAYBACK_SEVERITY_MASK) == MEL_AUDIOPLAYBACK_ERROR; }
static inline bool mel_audioplayback_status_warned(Mel_AudioPlayback_Status s) { return (s & MEL_AUDIOPLAYBACK_SEVERITY_MASK) == MEL_AUDIOPLAYBACK_WARNED; }

typedef struct Mel_AudioPlayback Mel_AudioPlayback;

typedef u32 (*Mel_AudioPlayback_Pull_Fn)(void* user, f32* interleaved_dst, u32 frames);

typedef struct
{
    u32                       sample_rate;
    u32                       channels;
    u32                       ring_capacity_frames;
    Mel_AudioPlayback_Pull_Fn pull;
    void*                     user;
    bool                      exclusive;
} Mel_AudioPlayback_Opt;

typedef struct
{
    Mel_AudioPlayback*       playback;
    Mel_AudioPlayback_Status status;
} Mel_AudioPlayback_Open_Result;

typedef struct
{
    bool exclusive;
    bool os_timestamps;
} Mel_AudioPlayback_Granted;

MEL_NODISCARD Mel_AudioPlayback_Open_Result mel_audioplayback_open(const Mel_Alloc* alloc, Mel_AudioOut device, Mel_AudioPlayback_Opt opt);

MEL_NODISCARD u32 mel_audioplayback_write(Mel_AudioPlayback* p, const f32* interleaved_src, u32 frames);
MEL_NODISCARD u32 mel_audioplayback_writable(const Mel_AudioPlayback* p);

Mel_AudioPlayback_Status  mel_audioplayback_status(const Mel_AudioPlayback* p);
Mel_AudioPlayback_Granted mel_audioplayback_granted(const Mel_AudioPlayback* p);
u32                       mel_audioplayback_latency_frames(const Mel_AudioPlayback* p);
u64                       mel_audioplayback_underrun_frames(const Mel_AudioPlayback* p);

void mel_audioplayback_close(Mel_AudioPlayback* p);

#ifdef __cplusplus
}
#endif
