#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <audio/engine.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Audio_Ring Mel_Audio_Ring;
typedef struct Mel_Event      Mel_Event;

bool mel_audio_backend_open(Mel_Audio_Opt req, Mel_Audio_Caps* granted, const Mel_Alloc* a);
void mel_audio_backend_start(Mel_Audio_Ring* ring);
void mel_audio_backend_stop(void);
void mel_audio_backend_close(const Mel_Alloc* a);
void mel_audio_backend_set_device_event(Mel_Event* ev);

u32 mel_audio_ring_read(Mel_Audio_Ring* r, f32* dst, u32 count);
u32 mel_audio_ring_read_available(const Mel_Audio_Ring* r);

#ifdef __cplusplus
}
#endif
