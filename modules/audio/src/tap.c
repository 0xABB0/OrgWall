#include <audio/tap.h>

#include "audio_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>
#include <collection/array.h>
#include <log/log.h>

#include <string.h>

static void mel_audio__tap_destroy(Mel_Audio_Tap* t)
{
    if (t->ring != NULL)
        mel_pcm_ring_destroy(t->ring);
    mel_dealloc(t->alloc, t);
}

void mel_audio__cmd_tap_attach(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio_Tap* t = cmd->instance;
    mel_array_push(&eng->taps, t);
    if (!mel_audio__tap_scratch_ensure(eng, eng->scratch_frames))
        mel_log_error("audio", "tap scratch allocation failed; tap will starve");
}

void mel_audio__cmd_tap_detach(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio_Tap* t = cmd->instance;
    for (usize i = 0; i < eng->taps.count; i++)
    {
        if (eng->taps.items[i] == t)
        {
            eng->taps.items[i] = eng->taps.items[eng->taps.count - 1u];
            eng->taps.count--;
            break;
        }
    }
    mel_audio__tap_destroy(t);
}

bool mel_audio__tap_scratch_ensure(Mel_Audio* eng, u32 frames)
{
    if (frames == 0u)
        return true;
    u32 channels = eng->caps.channels;
    if (eng->scratch_tap_frames >= frames && eng->scratch_tap_channels >= channels)
        return true;

    f32* planar = mel_alloc(eng->alloc, sizeof(f32) * (usize)channels * (usize)frames);
    f32* inter = mel_alloc(eng->alloc, sizeof(f32) * (usize)channels * (usize)frames);
    if (planar == NULL || inter == NULL)
    {
        if (planar != NULL)
            mel_dealloc(eng->alloc, planar);
        if (inter != NULL)
            mel_dealloc(eng->alloc, inter);
        return false;
    }

    if (eng->scratch_tap_planar != NULL)
        mel_dealloc(eng->alloc, eng->scratch_tap_planar);
    if (eng->scratch_tap_inter != NULL)
        mel_dealloc(eng->alloc, eng->scratch_tap_inter);
    eng->scratch_tap_planar = planar;
    eng->scratch_tap_inter = inter;
    eng->scratch_tap_frames = frames;
    eng->scratch_tap_channels = channels;
    return true;
}

static void mel_audio__tap_ring_write(Mel_Audio_Tap* t, const f32* interleaved, u32 frames)
{
    u32 accepted = mel_pcm_ring_write(t->ring, interleaved, frames);
    if (accepted < frames)
        atomic_fetch_add_explicit(&t->dropped, frames - accepted, memory_order_relaxed);
}

static void mel_audio__tap_interleave(Mel_Audio* eng, const f32* planar, u32 frames)
{
    u32 channels = eng->caps.channels;
    for (u32 c = 0; c < channels; c++)
    {
        const f32* plane = planar + (usize)c * frames;
        for (u32 i = 0; i < frames; i++)
            eng->scratch_tap_inter[(usize)i * channels + c] = plane[i];
    }
}

void mel_audio__taps_master_write(Mel_Audio* eng, const f32* planar_out, u32 frames)
{
    bool any = false;
    for (usize i = 0; i < eng->taps.count; i++)
        if (!eng->taps.items[i]->is_voice)
            any = true;
    if (!any)
        return;
    if (eng->scratch_tap_frames < frames)
        return;

    mel_audio__tap_interleave(eng, planar_out, frames);
    for (usize i = 0; i < eng->taps.count; i++)
        if (!eng->taps.items[i]->is_voice)
            mel_audio__tap_ring_write(eng->taps.items[i], eng->scratch_tap_inter, frames);
}

void mel_audio__taps_voice_write(Mel_Audio* eng, Mel_SlotMap_Handle voice, u32 src_channels, u32 frames, f32 gain_l, f32 gain_r)
{
    bool any = false;
    for (usize i = 0; i < eng->taps.count; i++)
    {
        Mel_Audio_Tap* t = eng->taps.items[i];
        if (t->is_voice && t->voice.index == voice.index && t->voice.generation == voice.generation)
            any = true;
    }
    if (!any)
        return;
    if (eng->scratch_tap_frames < frames)
        return;

    u32 out_channels = eng->caps.channels;
    memset(eng->scratch_tap_planar, 0, sizeof(f32) * (usize)out_channels * (usize)frames);
    mel_audio__pan_accumulate(eng->scratch_resampled, src_channels, frames, gain_l, gain_r, eng->scratch_tap_planar, out_channels);
    mel_audio__tap_interleave(eng, eng->scratch_tap_planar, frames);

    for (usize i = 0; i < eng->taps.count; i++)
    {
        Mel_Audio_Tap* t = eng->taps.items[i];
        if (t->is_voice && t->voice.index == voice.index && t->voice.generation == voice.generation)
            mel_audio__tap_ring_write(t, eng->scratch_tap_inter, frames);
    }
}

void mel_audio__taps_free_all(Mel_Audio* eng)
{
    if (eng->taps.count > 0u)
        mel_log_warn("audio", "destroy with %zu open tap(s); engine releasing them", eng->taps.count);
    for (usize i = 0; i < eng->taps.count; i++)
        mel_audio__tap_destroy(eng->taps.items[i]);
    eng->taps.count = 0;
}

static Mel_Audio_Tap* mel_audio__tap_open_impl(Mel_Audio* eng, Mel_SlotMap_Handle voice, u32 is_voice, const Mel_Alloc* a, u32 ring_frames)
{
    assert(eng != NULL);
    assert(a != NULL);
    assert(ring_frames > 0u);
    if (a == NULL || ring_frames == 0u)
    {
        mel_log_error("audio", "tap open with no allocator or zero ring_frames");
        return NULL;
    }

    if (!mel_audio__api_enter(eng))
        return NULL;

    Mel_Audio_Tap* t = mel_alloc_type(a, Mel_Audio_Tap);
    if (t == NULL)
    {
        mel_audio__api_leave(eng);
        return NULL;
    }
    memset(t, 0, sizeof *t);
    t->eng = eng;
    t->alloc = a;
    t->voice = voice;
    t->is_voice = is_voice;
    t->ring = mel_pcm_ring_create(a, eng->caps.channels, ring_frames);
    if (t->ring == NULL)
    {
        mel_dealloc(a, t);
        mel_audio__api_leave(eng);
        return NULL;
    }

    Mel_Audio__Command cmd = { .apply = mel_audio__cmd_tap_attach, .instance = t };
    if (eng->online == 0u)
        mel_audio__cmd_tap_attach(eng, &cmd);
    else
        mel_audio__command_push(eng, &cmd);

    mel_audio__api_leave(eng);
    return t;
}

Mel_Audio_Tap* mel_audio_tap_open(Mel_Audio* eng, const Mel_Alloc* a, u32 ring_frames) { return mel_audio__tap_open_impl(eng, MEL_SLOTMAP_HANDLE_NULL, 0u, a, ring_frames); }

Mel_Audio_Tap* mel_audio_voice_tap_open(Mel_Audio* eng, Mel_Audio_Voice v, const Mel_Alloc* a, u32 ring_frames)
{
    assert(eng != NULL);
    if (!mel_audio__voice_alive(&eng->voices, v.slot))
    {
        mel_log_error("audio", "voice tap open on a stale voice handle");
        return NULL;
    }
    return mel_audio__tap_open_impl(eng, v.slot, 1u, a, ring_frames);
}

u32 mel_audio_tap_read(Mel_Audio_Tap* t, f32* interleaved_dst, u32 max_frames)
{
    assert(t != NULL);
    assert(interleaved_dst != NULL);
    return mel_pcm_ring_read(t->ring, interleaved_dst, max_frames);
}

u32 mel_audio_tap_available(const Mel_Audio_Tap* t)
{
    assert(t != NULL);
    return mel_pcm_ring_read_available(t->ring);
}

u64 mel_audio_tap_dropped_frames(const Mel_Audio_Tap* t)
{
    assert(t != NULL);
    return atomic_load_explicit((_Atomic(u64)*)&t->dropped, memory_order_relaxed);
}

void mel_audio_tap_close(Mel_Audio_Tap* t)
{
    if (t == NULL)
        return;
    Mel_Audio* eng = t->eng;
    if (!mel_audio__api_enter(eng))
        return;

    Mel_Audio__Command cmd = { .apply = mel_audio__cmd_tap_detach, .instance = t };
    if (eng->online == 0u)
        mel_audio__cmd_tap_detach(eng, &cmd);
    else
        mel_audio__command_push(eng, &cmd);

    mel_audio__api_leave(eng);
}
