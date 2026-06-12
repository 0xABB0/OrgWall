#include <audio/engine.h>

#include "audio_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>
#include <audiopolicy/audiopolicy.h>
#include <log/log.h>

#include <string.h>

void mel_audio__device_event_fire(Mel_Audio* eng, Mel_Audio_Device_Event ev)
{
    if (eng->device_events != NULL)
        mel_event_fire(eng->device_events, &ev);
}

static u32 mel_audio__device_pull(void* user, f32* dst, u32 frames)
{
    Mel_Audio* eng = user;
    u32        channels = eng->caps.channels;

    u32 avail_frames = mel_audio_ring_read_available(eng->ring) / channels;
    u32 take = frames < avail_frames ? frames : avail_frames;
    u32 got = take > 0u ? mel_audio_ring_read(eng->ring, dst, take * channels) / channels : 0u;

    if (got < frames)
    {
        memset(dst + (usize)got * channels, 0, sizeof(f32) * (usize)(frames - got) * channels);
        atomic_fetch_add_explicit(&eng->device_underruns, frames - got, memory_order_relaxed);
        atomic_fetch_or_explicit(&eng->device_bits, MEL_AUDIO_WARN_RING_UNDERRUN, memory_order_release);
    }
    else
        atomic_fetch_and_explicit(&eng->device_bits, ~MEL_AUDIO_WARN_RING_UNDERRUN, memory_order_release);

    return frames;
}

bool mel_audio__device_open(Mel_Audio* eng, Mel_AudioOut target, Mel_Audio_Status* status)
{
    if (!mel_audioout_alive(target))
    {
        mel_log_error("audio", "device open on dead handle {index=%u, gen=%u}; was mel_audioout_init called?", target.h.index, target.h.generation);
        *status = MEL_AUDIO_ERROR | MEL_AUDIO_RESULT_NO_DEVICE;
        return false;
    }

    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(eng->alloc,
                                                             target,
                                                             (Mel_AudioPlayback_Opt){
                                                                 .sample_rate = eng->caps.samplerate,
                                                                 .channels = eng->caps.channels,
                                                                 .pull = mel_audio__device_pull,
                                                                 .user = eng,
                                                             });
    if (mel_audioplayback_status_failed(r.status))
    {
        u32 bit = MEL_AUDIO_RESULT_FORMAT_UNGRANTED;
        if (r.status & MEL_AUDIOPLAYBACK_RESULT_NO_DEVICE)
            bit = MEL_AUDIO_RESULT_NO_DEVICE;
        else if (r.status & MEL_AUDIOPLAYBACK_RESULT_BUSY)
            bit = MEL_AUDIO_RESULT_DEVICE_DENIED;
        mel_log_error("audio", "device open failed (audioplayback status 0x%x)", r.status);
        *status = MEL_AUDIO_ERROR | bit;
        return false;
    }

    eng->playback = r.playback;
    eng->bound = target;
    atomic_fetch_and_explicit(&eng->device_bits, ~(u32)(MEL_AUDIO_RESULT_DEVICE_LOST | MEL_AUDIO_RESULT_INTERRUPTED), memory_order_release);

    u32 warn = 0;
    u32 native_rate = 0;
    u32 native_channels = 0;

    Mel_AudioOut_Describe_Result d = mel_audioout_describe(target, eng->alloc);
    if (!mel_audioout_status_failed(d.status))
    {
        native_rate = d.value.samplerate;
        native_channels = d.value.channels;
        if (native_rate != 0 && native_rate != eng->caps.samplerate)
            warn |= MEL_AUDIO_WARN_RATE_RESAMPLED;
        if (native_channels != 0 && native_channels != eng->caps.channels)
            warn |= MEL_AUDIO_WARN_CHANNELS_REMIXED;
        mel_audioout_describe_free(&d);
    }
    eng->native_rate = native_rate;
    eng->native_channels = native_channels;

    u32 ring_frames = mel_audio_ring_capacity(eng->ring) / eng->caps.channels;
    eng->caps.latency_frames = mel_audioplayback_latency_frames(r.playback) + ring_frames;

    *status = warn != 0 ? (MEL_AUDIO_WARNED | warn) : MEL_AUDIO_OK;
    return true;
}

void mel_audio__device_close(Mel_Audio* eng)
{
    if (eng->playback != NULL)
    {
        mel_audioplayback_close(eng->playback);
        eng->playback = NULL;
    }
}

static void mel_audio__device_hold(Mel_Audio* eng, u32 bit)
{
    mel_audio__device_close(eng);
    atomic_fetch_or_explicit(&eng->device_bits, bit, memory_order_release);
}

static void mel_audio__on_hotplug(const Mel_AudioOut_Event* ev, void* user)
{
    Mel_Audio* eng = user;

    if (ev->removed && mel_audioout_equal(ev->device, eng->bound) && eng->playback != NULL && !eng->follow)
    {
        mel_log_warn("audio", "bound output device vanished; engine holds until rebind");
        mel_audio__device_hold(eng, MEL_AUDIO_RESULT_DEVICE_LOST);
        mel_audio__device_event_fire(eng, (Mel_Audio_Device_Event){ .device = eng->bound, .lost = true });
        return;
    }

    if (ev->default_changed && eng->follow)
    {
        Mel_AudioOut next = mel_audioout_default();
        u32          prev_rate = eng->native_rate;
        u32          prev_channels = eng->native_channels;

        if (!mel_audioout_alive(next))
        {
            mel_log_warn("audio", "system default output vanished; engine holds until one returns");
            mel_audio__device_hold(eng, MEL_AUDIO_RESULT_DEVICE_LOST);
            mel_audio__device_event_fire(eng, (Mel_Audio_Device_Event){ .device = eng->bound, .lost = true });
            return;
        }

        mel_audio__device_close(eng);
        Mel_Audio_Status st;
        if (!mel_audio__device_open(eng, next, &st))
        {
            atomic_fetch_or_explicit(&eng->device_bits, MEL_AUDIO_RESULT_DEVICE_LOST, memory_order_release);
            mel_audio__device_event_fire(eng, (Mel_Audio_Device_Event){ .device = next, .lost = true });
            return;
        }

        bool renegotiated = eng->native_rate != prev_rate || eng->native_channels != prev_channels;
        mel_audio__device_event_fire(eng,
                                     (Mel_Audio_Device_Event){
                                         .device = next,
                                         .default_changed = true,
                                         .format_changed = renegotiated,
                                     });
    }
}

static void mel_audio__on_policy(const Mel_AudioPolicy_Event* ev, void* user)
{
    Mel_Audio* eng = user;

    if (ev->interruption_began && eng->playback != NULL)
    {
        mel_log_info("audio", "OS interruption; engine holds");
        eng->interrupted = 1u;
        mel_audio__device_hold(eng, MEL_AUDIO_RESULT_INTERRUPTED);
        mel_audio__device_event_fire(eng, (Mel_Audio_Device_Event){ .device = eng->bound, .interrupted = true });
        return;
    }

    if (ev->interruption_ended && ev->should_resume && eng->interrupted)
    {
        eng->interrupted = 0u;
        Mel_AudioOut     target = eng->follow ? mel_audioout_default() : eng->bound;
        Mel_Audio_Status st;
        if (!mel_audio__device_open(eng, target, &st))
        {
            atomic_fetch_or_explicit(&eng->device_bits, MEL_AUDIO_RESULT_DEVICE_LOST, memory_order_release);
            mel_audio__device_event_fire(eng, (Mel_Audio_Device_Event){ .device = eng->bound, .lost = true });
            return;
        }
        mel_log_info("audio", "OS interruption ended; engine resumed");
        mel_audio__device_event_fire(eng, (Mel_Audio_Device_Event){ .device = eng->bound, .resumed = true });
    }
}

void mel_audio__device_subscribe(Mel_Audio* eng)
{
    eng->out_sub = mel_audioout_subscribe(eng->exec, mel_audio__on_hotplug, eng);
    if (mel_audiopolicy_active())
    {
        eng->policy_sub = mel_audiopolicy_subscribe(eng->exec, mel_audio__on_policy, eng);
        eng->policy_bound = 1u;
    }
}

void mel_audio__device_unsubscribe(Mel_Audio* eng)
{
    mel_audioout_unsubscribe(eng->out_sub);
    if (eng->policy_bound)
    {
        mel_audiopolicy_unsubscribe(eng->policy_sub);
        eng->policy_bound = 0u;
    }
}

Mel_Audio_Status mel_audio_set_device(Mel_Audio* eng, Mel_AudioOut device)
{
    assert(eng != NULL);
    assert(eng->online != 0u);
    if (eng->online == 0u)
    {
        mel_log_error("audio", "set_device on an offline engine; there is no device plane");
        return MEL_AUDIO_ERROR | MEL_AUDIO_RESULT_NO_DEVICE;
    }

    if (!mel_audio__api_enter(eng))
        return MEL_AUDIO_ERROR | MEL_AUDIO_RESULT_DESTROYING;

    bool         follow = mel_audioout_equal(device, MEL_AUDIOOUT_NULL);
    Mel_AudioOut target = follow ? mel_audioout_default() : device;

    Mel_AudioPlayback* previous = eng->playback;
    Mel_AudioOut       previous_bound = eng->bound;
    eng->playback = NULL;

    Mel_Audio_Status st;
    if (!mel_audio__device_open(eng, target, &st))
    {
        eng->playback = previous;
        eng->bound = previous_bound;
        mel_audio__api_leave(eng);
        return st;
    }

    if (previous != NULL)
        mel_audioplayback_close(previous);

    eng->follow = follow ? 1u : 0u;
    eng->interrupted = 0u;

    mel_audio__api_leave(eng);
    return st;
}

Mel_AudioOut mel_audio_device(const Mel_Audio* eng)
{
    assert(eng != NULL);
    return eng->bound;
}

Mel_Audio_Status mel_audio_device_status(const Mel_Audio* eng)
{
    assert(eng != NULL);
    if (eng->online == 0u)
        return MEL_AUDIO_OK;
    u32 bits = atomic_load_explicit((_Atomic(u32)*)&eng->device_bits, memory_order_acquire);
    if (bits & (MEL_AUDIO_RESULT_DEVICE_LOST | MEL_AUDIO_RESULT_INTERRUPTED))
        return MEL_AUDIO_ERROR | bits;
    if (bits != 0)
        return MEL_AUDIO_WARNED | bits;
    return MEL_AUDIO_OK;
}
