#include <audiomixer/engine.h>

#include "mixer_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>
#include <collection/array.h>
#include <log/log.h>

#include <string.h>

u32 mel_mixer__fetch_frames_for(u32 frames, f64 ratio, f64 cursor_frac)
{
    if (frames == 0u)
        return 0u;
    f64 span = cursor_frac + (f64)frames * ratio;
    u32 consumed = (u32)span;
    return consumed > 0u ? consumed : 1u;
}

static void mel_mixer__scratch_free(Mel_Mixer* eng)
{
    if (eng->scratch_planar != NULL)
        mel_dealloc(eng->alloc, eng->scratch_planar);
    if (eng->scratch_voice != NULL)
        mel_dealloc(eng->alloc, eng->scratch_voice);
    if (eng->scratch_resampled != NULL)
        mel_dealloc(eng->alloc, eng->scratch_resampled);
    eng->scratch_planar = NULL;
    eng->scratch_voice = NULL;
    eng->scratch_resampled = NULL;
}

static bool mel_mixer__scratch_alloc(Mel_Mixer* eng, u32 frames, u32 fetch_frames, u32 channels)
{
    u32 planar_frames = frames > fetch_frames ? frames : fetch_frames;

    f32* planar = mel_calloc(eng->alloc, sizeof(f32) * (usize)channels * (usize)planar_frames);
    f32* voice = mel_calloc(eng->alloc, sizeof(f32) * (usize)channels * (usize)fetch_frames);
    f32* resampled = mel_calloc(eng->alloc, sizeof(f32) * (usize)channels * (usize)frames);

    if (planar == NULL || voice == NULL || resampled == NULL)
    {
        if (planar != NULL)
            mel_dealloc(eng->alloc, planar);
        if (voice != NULL)
            mel_dealloc(eng->alloc, voice);
        if (resampled != NULL)
            mel_dealloc(eng->alloc, resampled);
        return false;
    }

    mel_mixer__scratch_free(eng);
    eng->scratch_planar = planar;
    eng->scratch_voice = voice;
    eng->scratch_resampled = resampled;
    eng->scratch_frames = planar_frames;
    eng->scratch_fetch_frames = fetch_frames;
    eng->scratch_channels = channels;
    return true;
}

void mel_mixer__scratch_size_worst(Mel_Mixer* eng, u32 frames)
{
    u32 channels = eng->worst_channels;
    u32 fetch = mel_mixer__fetch_frames_for(frames, eng->worst_ratio, 1.0) + 2u;
    if (!mel_mixer__scratch_alloc(eng, frames, fetch, channels))
    {
        mel_log_error("audio", "scratch alloc failed (%u frames, %u fetch, %u ch)", frames, fetch, channels);
        assert(!"mel audio: scratch allocation failed");
    }
}

void mel_mixer__scratch_ensure_offline(Mel_Mixer* eng, u32 frames)
{
    u32 channels = eng->caps.channels;
    f64 max_ratio = 1.0;
    for (u32 i = 0; i < (u32)eng->voices.packed.count; i++)
    {
        Mel_Mixer__Voice* v = &eng->voices.packed.items[i];
        if (v->source == NULL)
            continue;
        if (v->source->channels > channels)
            channels = v->source->channels;
        f64 ratio = (v->source->base_samplerate / (f64)eng->caps.samplerate) * v->play_speed;
        if (ratio > max_ratio)
            max_ratio = ratio;
    }

    u32 fetch = mel_mixer__fetch_frames_for(frames, max_ratio, 1.0) + 2u;
    if (eng->taps.count > 0u && !mel_mixer__tap_scratch_ensure(eng, frames))
        mel_log_error("audio", "offline tap scratch alloc failed (%u frames)", frames);
    if (eng->scratch_frames >= frames && eng->scratch_fetch_frames >= fetch && eng->scratch_channels >= channels)
        return;

    u32 want_frames = eng->scratch_frames > frames ? eng->scratch_frames : frames;
    u32 want_fetch = eng->scratch_fetch_frames > fetch ? eng->scratch_fetch_frames : fetch;
    u32 want_channels = eng->scratch_channels > channels ? eng->scratch_channels : channels;

    if (!mel_mixer__scratch_alloc(eng, want_frames, want_fetch, want_channels))
    {
        mel_log_error("audio", "offline scratch alloc failed (%u frames, %u fetch, %u ch)", want_frames, want_fetch, want_channels);
        assert(!"mel audio: offline scratch allocation failed");
    }

    if (eng->taps.count > 0u && !mel_mixer__tap_scratch_ensure(eng, want_frames))
        mel_log_error("audio", "offline tap scratch alloc failed (%u frames)", want_frames);
}

static void mel_mixer__free_engine_shell(Mel_Mixer* eng)
{
    mel_mixer__end_futures_free(eng);
    if (eng->device_events != NULL)
        mel_event_destroy(eng->device_events);
    mel_mixer__voices_free(&eng->voices);
    mel_array_free(&eng->commands);
    mel_array_free(&eng->commands_back);
    mel_array_free(&eng->taps);
    if (eng->scratch_tap_planar != NULL)
        mel_dealloc(eng->alloc, eng->scratch_tap_planar);
    if (eng->scratch_tap_inter != NULL)
        mel_dealloc(eng->alloc, eng->scratch_tap_inter);
    mel_dealloc(eng->alloc, eng);
}

static Mel_Mixer* mel_mixer__alloc_engine(const Mel_Alloc* a, Mel_Mixer_Opt opt)
{
    Mel_Mixer* eng = mel_alloc(a, sizeof(*eng));
    if (eng == NULL)
    {
        mel_log_error("audio", "engine struct alloc failed");
        return NULL;
    }

    *eng = (Mel_Mixer){ 0 };
    eng->alloc = a;
    eng->exec = opt.exec;
    eng->resampler = opt.resampler != NULL ? opt.resampler : mel_mixer_resample_linear;
    eng->master_volume = opt.master_volume;
    eng->stream_clock = 0.0;
    atomic_store_explicit(&eng->destroying, 0u, memory_order_relaxed);
    atomic_store_explicit(&eng->api_inflight, 0u, memory_order_relaxed);

    mel_mixer__voices_init(&eng->voices, a, 16u);
    mel_array_init(&eng->commands, a);
    mel_array_init(&eng->commands_back, a);
    mel_array_init(&eng->end_futures, a);
    mel_array_init(&eng->taps, a);

    eng->command_lock = (Mel_Spinlock){ 0 };

    eng->device_events = mel_event_create(a, sizeof(Mel_Mixer_Device_Event), 8u, mel_event_policy_latest(NULL, NULL));
    if (eng->device_events == NULL)
    {
        mel_log_error("audio", "device-events event create failed");
        mel_mixer__voices_free(&eng->voices);
        mel_array_free(&eng->commands);
        mel_array_free(&eng->commands_back);
        mel_array_free(&eng->end_futures);
        mel_array_free(&eng->taps);
        mel_dealloc(a, eng);
        return NULL;
    }

    return eng;
}

Mel_Mixer* mel_mixer_create_offline(const Mel_Alloc* a, Mel_Mixer_Opt opt)
{
    assert(a != NULL);
    assert(opt.samplerate > 0);
    assert(opt.channels >= 1u);

    Mel_Mixer* eng = mel_mixer__alloc_engine(a, opt);
    if (eng == NULL)
        return NULL;

    eng->online = 0u;
    eng->caps = (Mel_Mixer_Caps){
        .samplerate = opt.samplerate,
        .channels = opt.channels,
        .block_frames = opt.block_frames,
        .ring_blocks = 0u,
        .latency_frames = 0u,
    };

    return eng;
}

static int mel_mixer__mix_thread_fn(void* user)
{
    Mel_Mixer* eng = (Mel_Mixer*)user;
    u32        block = eng->caps.block_frames;
    u32        channels = eng->caps.channels;
    u32        block_samples = block * channels;

    f32* interleave = mel_calloc(eng->alloc, sizeof(f32) * (usize)block_samples);
    if (interleave == NULL)
    {
        mel_log_error("audio", "mix thread interleave alloc failed");
        assert(!"mel audio: mix thread interleave allocation failed");
        return 1;
    }

    while (atomic_load_explicit(&eng->mix_stop, memory_order_acquire) == 0u)
    {
        mel_mixer__commands_drain(eng);

        while (mel_mixer_ring_write_available(eng->ring) >= block_samples)
        {
            mel_mixer__mix_block(eng, eng->scratch_planar, block);
            for (u32 c = 0; c < channels; c++)
            {
                const f32* plane = eng->scratch_planar + (usize)c * block;
                for (u32 i = 0; i < block; i++)
                    interleave[(usize)i * channels + c] = plane[i];
            }
            mel_mixer_ring_write(eng->ring, interleave, block_samples);
        }

        if (atomic_load_explicit(&eng->mix_stop, memory_order_acquire) != 0u)
            break;
        if (mel_mixer_ring_write_available(eng->ring) < block_samples)
            mel_sem_wait(&eng->mix_wake);
    }

    mel_dealloc(eng->alloc, interleave);
    return 0;
}

Mel_Mixer* mel_mixer_create(const Mel_Alloc* a, Mel_Mixer_Opt opt)
{
    assert(a != NULL);
    assert(opt.samplerate > 0);
    assert(opt.channels >= 1u);
    assert(opt.block_frames > 0);
    assert(opt.ring_blocks > 0u);
    assert(opt.max_voice_channels >= opt.channels);
    assert(opt.max_voice_ratio >= 1.0);

    Mel_Mixer* eng = mel_mixer__alloc_engine(a, opt);
    if (eng == NULL)
        return NULL;

    eng->online = 1u;

    eng->caps = (Mel_Mixer_Caps){
        .samplerate = opt.samplerate,
        .channels = opt.channels,
        .block_frames = opt.block_frames,
        .ring_blocks = opt.ring_blocks,
        .latency_frames = 0u,
    };
    eng->worst_channels = opt.max_voice_channels > opt.channels ? opt.max_voice_channels : opt.channels;
    eng->worst_ratio = opt.max_voice_ratio;

    u32 ring_samples = opt.ring_blocks * opt.block_frames * opt.channels;
    eng->ring = mel_mixer_ring_create(a, ring_samples);
    if (eng->ring == NULL)
    {
        mel_log_error("audio", "ring alloc failed (%u samples)", ring_samples);
        mel_mixer__free_engine_shell(eng);
        return NULL;
    }

    mel_mixer__scratch_size_worst(eng, opt.block_frames);

    atomic_store_explicit(&eng->mix_stop, 0u, memory_order_relaxed);
    if (!mel_sem_init(&eng->mix_wake, 0u))
    {
        mel_log_error("audio", "mix wake semaphore init failed");
        mel_mixer__scratch_free(eng);
        mel_mixer_ring_destroy(eng->ring);
        mel_mixer__free_engine_shell(eng);
        return NULL;
    }

    mel_mixer_ring_set_wake(eng->ring, &eng->mix_wake);

    if (!mel_thread_spawn(&eng->mix_thread, mel_mixer__mix_thread_fn, eng, .name = "mel-audio-mix"))
    {
        mel_log_error("audio", "mix thread spawn failed");
        mel_mixer_ring_set_wake(eng->ring, NULL);
        mel_sem_destroy(&eng->mix_wake);
        mel_mixer__scratch_free(eng);
        mel_mixer_ring_destroy(eng->ring);
        mel_mixer__free_engine_shell(eng);
        return NULL;
    }

    bool         follow = mel_audioout_equal(opt.device, MEL_AUDIOOUT_NULL);
    Mel_AudioOut target = follow ? mel_audioout_default() : opt.device;

    Mel_Mixer_Status st;
    if (!mel_mixer__device_open(eng, target, &st))
    {
        mel_log_error("audio", "create denied: no usable output device (status 0x%x)", st);
        atomic_store_explicit(&eng->mix_stop, 1u, memory_order_release);
        mel_sem_post(&eng->mix_wake);
        mel_thread_join(&eng->mix_thread, NULL);
        mel_mixer_ring_set_wake(eng->ring, NULL);
        mel_sem_destroy(&eng->mix_wake);
        mel_mixer__scratch_free(eng);
        mel_mixer_ring_destroy(eng->ring);
        mel_mixer__free_engine_shell(eng);
        return NULL;
    }
    eng->follow = follow ? 1u : 0u;

    if (mel_mixer_warned(st))
        mel_log_warn("audio", "device format differs from the engine's (status 0x%x); audioplayback converts", st);

    mel_mixer__device_subscribe(eng);
    return eng;
}

void mel_mixer_destroy(Mel_Mixer* eng)
{
    if (eng == NULL)
        return;

    atomic_store_explicit(&eng->destroying, 1u, memory_order_seq_cst);

    if (eng->online)
    {
        mel_mixer__device_unsubscribe(eng);
        mel_mixer__device_close(eng);
        atomic_store_explicit(&eng->mix_stop, 1u, memory_order_release);
        mel_sem_post(&eng->mix_wake);
        mel_thread_join(&eng->mix_thread, NULL);
        mel_mixer_ring_set_wake(eng->ring, NULL);
        mel_sem_destroy(&eng->mix_wake);
        if (eng->ring != NULL)
            mel_mixer_ring_destroy(eng->ring);
    }

    while (atomic_load_explicit(&eng->api_inflight, memory_order_seq_cst) != 0u)
        ;

    mel_mixer__commands_drain(eng);

    if (eng->voices.packed.count > 0u)
        mel_log_warn("audio", "destroy with %zu live voice(s); engine releasing their instances (§7)", eng->voices.packed.count);

    while (eng->voices.packed.count > 0u)
        mel_mixer__voice_remove(eng, mel_array_last(&eng->voices.packed).self);

    mel_mixer__taps_free_all(eng);

    mel_mixer__scratch_free(eng);

    mel_mixer__free_engine_shell(eng);
}

Mel_Mixer_Caps mel_mixer_caps(const Mel_Mixer* eng)
{
    assert(eng != NULL);
    return eng->caps;
}

void mel_mixer_set_master_volume(Mel_Mixer* eng, f32 v)
{
    assert(eng != NULL);
    eng->master_fade.active = 0u;
    eng->master_volume = v;
}

u32 mel_mixer_active_voice_count(const Mel_Mixer* eng)
{
    assert(eng != NULL);
    return mel_mixer__voice_count(&eng->voices);
}
