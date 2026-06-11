#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <allocator/allocator.fwd.h>
#include <audioin/audioin.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_AudioCapture_Status;

#define MEL_AUDIOCAPTURE_SEVERITY_MASK 0x3u
#define MEL_AUDIOCAPTURE_OK            0u
#define MEL_AUDIOCAPTURE_WARNED        1u
#define MEL_AUDIOCAPTURE_ERROR         2u

#define MEL_AUDIOCAPTURE_RESULT_DENIED      (1u << 2)
#define MEL_AUDIOCAPTURE_RESULT_NO_DEVICE   (1u << 3)
#define MEL_AUDIOCAPTURE_RESULT_UNSUPPORTED (1u << 4)
#define MEL_AUDIOCAPTURE_RESULT_BUSY        (1u << 5)
#define MEL_AUDIOCAPTURE_RESULT_LOST        (1u << 6)

#define MEL_AUDIOCAPTURE_WARN_CONVERTED          (1u << 7)
#define MEL_AUDIOCAPTURE_WARN_OVERRUN            (1u << 8)
#define MEL_AUDIOCAPTURE_WARN_PROCESSING_DROPPED (1u << 9)
#define MEL_AUDIOCAPTURE_WARN_EXCLUSIVE_DROPPED  (1u << 10)

static inline bool mel_audiocapture_status_failed(Mel_AudioCapture_Status s) { return (s & MEL_AUDIOCAPTURE_SEVERITY_MASK) == MEL_AUDIOCAPTURE_ERROR; }
static inline bool mel_audiocapture_status_warned(Mel_AudioCapture_Status s) { return (s & MEL_AUDIOCAPTURE_SEVERITY_MASK) == MEL_AUDIOCAPTURE_WARNED; }

typedef struct Mel_AudioCapture Mel_AudioCapture;

typedef struct
{
    bool echo_cancellation;
    bool noise_suppression;
    bool auto_gain;
} Mel_AudioCapture_Processing;

typedef struct
{
    u32                         sample_rate;
    u32                         channels;
    u32                         ring_capacity_frames;
    bool                        exclusive;
    Mel_AudioCapture_Processing processing;
} Mel_AudioCapture_Opt;

typedef struct
{
    Mel_AudioCapture*       capture;
    Mel_AudioCapture_Status status;
} Mel_AudioCapture_Open_Result;

typedef struct
{
    Mel_AudioCapture_Processing processing;
    bool                        exclusive;
    bool                        os_timestamps;
} Mel_AudioCapture_Granted;

typedef struct
{
    u32 frames;
    u64 timestamp_ns;
} Mel_AudioCapture_Read;

MEL_NODISCARD Mel_AudioCapture_Open_Result mel_audiocapture_open(const Mel_Alloc* alloc, Mel_AudioIn device, Mel_AudioCapture_Opt opt);

MEL_NODISCARD u32                   mel_audiocapture_read(Mel_AudioCapture* c, f32* interleaved_dst, u32 max_frames);
MEL_NODISCARD Mel_AudioCapture_Read mel_audiocapture_read_ex(Mel_AudioCapture* c, f32* interleaved_dst, u32 max_frames);
MEL_NODISCARD u32                   mel_audiocapture_available(const Mel_AudioCapture* c);

Mel_AudioCapture_Status  mel_audiocapture_status(const Mel_AudioCapture* c);
Mel_AudioCapture_Granted mel_audiocapture_granted(const Mel_AudioCapture* c);
u64                      mel_audiocapture_dropped_frames(const Mel_AudioCapture* c);

void mel_audiocapture_close(Mel_AudioCapture* c);

#ifdef __cplusplus
}
#endif
