#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <allocator/allocator.h>
#include <string/str8.h>

typedef struct Mel_AudioCapture_Opt Mel_AudioCapture_Opt;

struct Mel_AudioCapture_Opt
{
    u32 sample_rate;
    u32 ring_capacity_frames;
};

typedef struct Mel_AudioCapture Mel_AudioCapture;

i32 mel_audiocapture_enumerate(u32* out_ids, i32 max_count);

MEL_NODISCARD str8 mel_audiocapture_device_name(u32 id, const Mel_Alloc* alloc);

MEL_NODISCARD bool mel_audiocapture_default_device(u32* out_id);

MEL_NODISCARD bool mel_audiocapture_authorized(void);
MEL_NODISCARD bool mel_audiocapture_auth_determined(void);

MEL_NODISCARD Mel_AudioCapture* mel_audiocapture_open(const Mel_Alloc* alloc, u32 device_id, Mel_AudioCapture_Opt opt);

MEL_NODISCARD u32 mel_audiocapture_read(Mel_AudioCapture* c, f32* dst, u32 max_frames);

MEL_NODISCARD u32 mel_audiocapture_available(const Mel_AudioCapture* c);

void mel_audiocapture_close(Mel_AudioCapture* c);
