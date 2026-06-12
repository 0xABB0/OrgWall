#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Audio_Source
{
    u32   channels;
    f64   base_samplerate;
    bool  single_instance;
    usize instance_size;
    bool (*instance_open)(struct Mel_Audio_Source* src);
    void (*instance_init)(struct Mel_Audio_Source* src, void* inst, const Mel_Alloc* a);
    u32 (*get_audio)(struct Mel_Audio_Source* src, void* inst, f32* planar_dst, u32 frames);
    void (*seek)(struct Mel_Audio_Source* src, void* inst, f64 seconds);
    void (*instance_free)(struct Mel_Audio_Source* src, void* inst, const Mel_Alloc* a);
    void (*source_free)(struct Mel_Audio_Source* src, const Mel_Alloc* a);
} Mel_Audio_Source;

#ifdef __cplusplus
}
#endif
