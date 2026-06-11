#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Pcm_Ring Mel_Pcm_Ring;

Mel_Pcm_Ring* mel_pcm_ring_create(const Mel_Alloc* a, u32 channels, u32 capacity_frames);
void          mel_pcm_ring_destroy(Mel_Pcm_Ring* r);

u32 mel_pcm_ring_write(Mel_Pcm_Ring* r, const f32* interleaved_src, u32 frames);
u32 mel_pcm_ring_read(Mel_Pcm_Ring* r, f32* interleaved_dst, u32 frames);

u32 mel_pcm_ring_channels(const Mel_Pcm_Ring* r);
u32 mel_pcm_ring_capacity(const Mel_Pcm_Ring* r);
u32 mel_pcm_ring_read_available(const Mel_Pcm_Ring* r);
u32 mel_pcm_ring_write_available(const Mel_Pcm_Ring* r);

#ifdef __cplusplus
}
#endif
