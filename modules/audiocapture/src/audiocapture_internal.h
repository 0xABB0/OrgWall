#pragma once

#include <stdatomic.h>

#include <audiocapture/audiocapture.h>

typedef struct Mel_AC_Ring Mel_AC_Ring;

struct Mel_AC_Ring
{
    f32*         samples;
    u32          capacity;
    _Atomic(u32) head;
    _Atomic(u32) tail;
};

void mel_ac_ring_init(Mel_AC_Ring* r, const Mel_Alloc* alloc, u32 capacity);
void mel_ac_ring_free(Mel_AC_Ring* r, const Mel_Alloc* alloc);
u32  mel_ac_ring_write(Mel_AC_Ring* r, const f32* src, u32 count);
u32  mel_ac_ring_read(Mel_AC_Ring* r, f32* dst, u32 count);
u32  mel_ac_ring_available(const Mel_AC_Ring* r);

struct Mel_AudioCapture
{
    const Mel_Alloc*     alloc;
    Mel_AudioCapture_Opt opt;
    Mel_AC_Ring          ring;
    void*                backend;
};
