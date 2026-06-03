#include <audio/pcm.h>

#include "audio_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>

#include <string.h>

typedef struct
{
    Mel_Audio_Source    base;
    const f32*          samples;
    f32*                owned;
    u32                 frames;
    u32                 channels;
    u32                 samplerate;
    Mel_Audio_Ownership ownership;
    u32                 loop;
    u32                 loop_start_frame;
} Mel_Audio__Pcm;

typedef struct
{
    u32 playhead;
} Mel_Audio__Pcm_Instance;

static u32 mel_audio__pcm_get_audio(Mel_Audio_Source* src, void* inst, f32* planar_dst, u32 frames)
{
    Mel_Audio__Pcm*          pcm = (Mel_Audio__Pcm*)src;
    Mel_Audio__Pcm_Instance* st = (Mel_Audio__Pcm_Instance*)inst;

    u32 channels = pcm->channels;
    u32 written = 0;

    while (written < frames)
    {
        if (st->playhead >= pcm->frames)
        {
            if (!pcm->loop)
                break;
            st->playhead = pcm->loop_start_frame;
            if (st->playhead >= pcm->frames)
                break;
        }

        u32 run = pcm->frames - st->playhead;
        u32 want = frames - written;
        if (run > want)
            run = want;

        for (u32 c = 0; c < channels; c++)
        {
            f32*       plane = planar_dst + (usize)c * frames + written;
            const f32* base = pcm->samples + (usize)st->playhead * channels + c;
            for (u32 i = 0; i < run; i++)
                plane[i] = base[(usize)i * channels];
        }

        st->playhead += run;
        written += run;
    }

    return written;
}

static void mel_audio__pcm_seek(Mel_Audio_Source* src, void* inst, f64 seconds)
{
    Mel_Audio__Pcm*          pcm = (Mel_Audio__Pcm*)src;
    Mel_Audio__Pcm_Instance* st = (Mel_Audio__Pcm_Instance*)inst;

    if (seconds < 0.0)
        seconds = 0.0;
    f64 frame = seconds * (f64)pcm->samplerate;
    u32 idx = (u32)frame;
    st->playhead = idx;
}

static void mel_audio__pcm_source_free(Mel_Audio_Source* src, const Mel_Alloc* a)
{
    Mel_Audio__Pcm* pcm = (Mel_Audio__Pcm*)src;
    if (pcm->ownership == MEL_AUDIO_OWNERSHIP_OWNED && pcm->owned != NULL)
        mel_dealloc(a, pcm->owned);
    mel_dealloc(a, pcm);
}

Mel_Audio_Source* mel_audio_pcm_from_float(const Mel_Alloc* a, const f32* interleaved, u32 frames, u32 channels, u32 samplerate, Mel_Audio_Ownership own)
{
    assert(a != NULL);
    assert(interleaved != NULL);
    assert(frames > 0);
    assert(channels >= 1u);
    assert(samplerate > 0);
    assert(own == MEL_AUDIO_OWNERSHIP_OWNED || own == MEL_AUDIO_OWNERSHIP_BORROWED);

    Mel_Audio__Pcm* pcm = mel_alloc(a, sizeof(*pcm));
    if (pcm == NULL)
        return NULL;

    *pcm = (Mel_Audio__Pcm){ 0 };

    if (own == MEL_AUDIO_OWNERSHIP_OWNED)
    {
        usize bytes = sizeof(f32) * (usize)frames * (usize)channels;
        pcm->owned = mel_alloc(a, bytes);
        if (pcm->owned == NULL)
        {
            mel_dealloc(a, pcm);
            return NULL;
        }
        memcpy(pcm->owned, interleaved, bytes);
        pcm->samples = pcm->owned;
    }
    else
    {
        pcm->samples = interleaved;
        pcm->owned = NULL;
    }

    pcm->frames = frames;
    pcm->channels = channels;
    pcm->samplerate = samplerate;
    pcm->ownership = own;
    pcm->loop = 0u;
    pcm->loop_start_frame = 0u;

    pcm->base.channels = channels;
    pcm->base.base_samplerate = (f64)samplerate;
    pcm->base.single_instance = false;
    pcm->base.instance_size = sizeof(Mel_Audio__Pcm_Instance);
    pcm->base.instance_init = NULL;
    pcm->base.get_audio = mel_audio__pcm_get_audio;
    pcm->base.seek = mel_audio__pcm_seek;
    pcm->base.instance_free = NULL;
    pcm->base.source_free = mel_audio__pcm_source_free;

    return &pcm->base;
}

void mel_audio_pcm_set_loop(Mel_Audio_Source* s, bool loop, f64 loop_start_seconds)
{
    assert(s != NULL);
    Mel_Audio__Pcm* pcm = (Mel_Audio__Pcm*)s;
    pcm->loop = loop ? 1u : 0u;

    if (loop_start_seconds < 0.0)
        loop_start_seconds = 0.0;
    u32 idx = (u32)(loop_start_seconds * (f64)pcm->samplerate);
    if (idx >= pcm->frames)
        idx = 0u;
    pcm->loop_start_frame = idx;
}
