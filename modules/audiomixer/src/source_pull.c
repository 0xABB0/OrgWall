#include <audiomixer/pcm.h>

#include "mixer_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>
#include <log/log.h>

#include <string.h>

typedef struct
{
    Mel_Mixer_Source  base;
    Mel_Mixer_Pull_Fn fn;
    void*             user;
    _Atomic(u32)      live;
} Mel_Mixer__Pull_Source;

typedef struct
{
    const Mel_Alloc* alloc;
    f32*             inter;
    u32              cap_frames;
} Mel_Mixer__Pull_Instance;

static bool mel_mixer__pull_instance_open(Mel_Mixer_Source* src)
{
    Mel_Mixer__Pull_Source* ps = (Mel_Mixer__Pull_Source*)src;
    u32                     prev = atomic_fetch_add_explicit(&ps->live, 1u, memory_order_acq_rel);
    if (prev != 0u)
    {
        atomic_fetch_sub_explicit(&ps->live, 1u, memory_order_acq_rel);
        mel_log_error("audio", "pull source already drives a voice; one producer, one voice");
        return false;
    }
    return true;
}

static void mel_mixer__pull_instance_init(Mel_Mixer_Source* src, void* inst, const Mel_Alloc* a)
{
    MEL_UNUSED(src);
    Mel_Mixer__Pull_Instance* st = inst;
    st->alloc = a;
    st->inter = NULL;
    st->cap_frames = 0u;
}

static u32 mel_mixer__pull_get_audio(Mel_Mixer_Source* src, void* inst, f32* planar_dst, u32 frames)
{
    Mel_Mixer__Pull_Source*   ps = (Mel_Mixer__Pull_Source*)src;
    Mel_Mixer__Pull_Instance* st = inst;
    u32                       channels = ps->base.channels;

    if (st->cap_frames < frames)
    {
        u32  cap = frames * 2u;
        f32* inter = mel_alloc(st->alloc, sizeof(f32) * (usize)cap * channels);
        if (inter == NULL)
        {
            mel_log_error("audio", "pull source scratch alloc failed (%u frames)", frames);
            for (u32 c = 0; c < channels; c++)
                memset(planar_dst + (usize)c * frames, 0, sizeof(f32) * frames);
            return frames;
        }
        if (st->inter != NULL)
            mel_dealloc(st->alloc, st->inter);
        st->inter = inter;
        st->cap_frames = cap;
    }

    u32 got = ps->fn(ps->user, st->inter, frames);
    if (got > frames)
        got = frames;

    for (u32 c = 0; c < channels; c++)
    {
        f32* plane = planar_dst + (usize)c * frames;
        for (u32 i = 0; i < got; i++)
            plane[i] = st->inter[(usize)i * channels + c];
        if (got < frames)
            memset(plane + got, 0, sizeof(f32) * (usize)(frames - got));
    }

    return frames;
}

static void mel_mixer__pull_instance_free(Mel_Mixer_Source* src, void* inst, const Mel_Alloc* a)
{
    MEL_UNUSED(a);
    Mel_Mixer__Pull_Source*   ps = (Mel_Mixer__Pull_Source*)src;
    Mel_Mixer__Pull_Instance* st = inst;
    if (st != NULL && st->inter != NULL)
        mel_dealloc(st->alloc, st->inter);
    atomic_fetch_sub_explicit(&ps->live, 1u, memory_order_acq_rel);
}

static void mel_mixer__pull_source_free(Mel_Mixer_Source* src, const Mel_Alloc* a)
{
    Mel_Mixer__Pull_Source* ps = (Mel_Mixer__Pull_Source*)src;
    mel_dealloc(a, ps);
}

Mel_Mixer_Source* mel_mixer_pull_source(const Mel_Alloc* a, Mel_Mixer_Pull_Fn fn, void* user, u32 channels, u32 samplerate)
{
    assert(a != NULL);
    assert(fn != NULL);
    assert(channels >= 1u);
    assert(samplerate > 0u);
    if (a == NULL || fn == NULL || channels == 0u || samplerate == 0u)
    {
        mel_log_error("audio", "pull source with NULL fn or zero channels/samplerate: the format is mandatory");
        return NULL;
    }

    Mel_Mixer__Pull_Source* ps = mel_alloc(a, sizeof(*ps));
    if (ps == NULL)
        return NULL;

    *ps = (Mel_Mixer__Pull_Source){ 0 };
    ps->fn = fn;
    ps->user = user;
    atomic_store_explicit(&ps->live, 0u, memory_order_relaxed);

    ps->base.channels = channels;
    ps->base.base_samplerate = (f64)samplerate;
    ps->base.single_instance = true;
    ps->base.instance_size = sizeof(Mel_Mixer__Pull_Instance);
    ps->base.instance_open = mel_mixer__pull_instance_open;
    ps->base.instance_init = mel_mixer__pull_instance_init;
    ps->base.get_audio = mel_mixer__pull_get_audio;
    ps->base.seek = NULL;
    ps->base.instance_free = mel_mixer__pull_instance_free;
    ps->base.source_free = mel_mixer__pull_source_free;

    return &ps->base;
}
