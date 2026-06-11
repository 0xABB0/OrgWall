#include <audio/backend.h>

#include "../../src/audio_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>
#include <thread/thread.h>
#include <log/log.h>

#include <alsa/asoundlib.h>
#include <stdatomic.h>

typedef struct
{
    snd_pcm_t*       pcm;
    Mel_Audio_Ring*  ring;
    const Mel_Alloc* alloc;
    f32*             staging;
    u32              staging_samples;
    u32              block_frames;
    u32              channels;
    Mel_Thread       writer;
    _Atomic(u32)     running;
    _Atomic(u64)     xruns;
    u32              spawned;
} Mel_Alsa_Backend;

static Mel_Alsa_Backend mel_audio__alsa = { 0 };

static bool mel_audio__alsa_check(int rc, const char* what)
{
    if (rc < 0)
    {
        mel_log_error("audio.alsa", "%s failed: %s", what, snd_strerror(rc));
        return false;
    }
    return true;
}

bool mel_audio_backend_open(Mel_Audio_Opt req, Mel_Audio_Caps* granted, const Mel_Alloc* a)
{
    assert(granted != NULL);
    assert(a != NULL);
    assert(req.samplerate > 0u);
    assert(req.channels >= 1u);
    assert(req.block_frames > 0u);

    Mel_Alsa_Backend* be = &mel_audio__alsa;
    *be = (Mel_Alsa_Backend){ 0 };
    be->alloc = a;

    int rc = snd_pcm_open(&be->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (!mel_audio__alsa_check(rc, "snd_pcm_open(default, PLAYBACK)"))
        return false;

    snd_pcm_hw_params_t* hw = NULL;
    snd_pcm_hw_params_alloca(&hw);

    rc = snd_pcm_hw_params_any(be->pcm, hw);
    if (!mel_audio__alsa_check(rc, "snd_pcm_hw_params_any"))
        goto fail;

    rc = snd_pcm_hw_params_set_access(be->pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (!mel_audio__alsa_check(rc, "snd_pcm_hw_params_set_access(RW_INTERLEAVED)"))
        goto fail;

    rc = snd_pcm_hw_params_set_format(be->pcm, hw, SND_PCM_FORMAT_FLOAT_LE);
    if (!mel_audio__alsa_check(rc, "snd_pcm_hw_params_set_format(FLOAT_LE)"))
        goto fail;

    unsigned int channels = req.channels;
    rc = snd_pcm_hw_params_set_channels_near(be->pcm, hw, &channels);
    if (!mel_audio__alsa_check(rc, "snd_pcm_hw_params_set_channels_near"))
        goto fail;

    unsigned int rate = req.samplerate;
    int          rate_dir = 0;
    rc = snd_pcm_hw_params_set_rate_near(be->pcm, hw, &rate, &rate_dir);
    if (!mel_audio__alsa_check(rc, "snd_pcm_hw_params_set_rate_near"))
        goto fail;

    snd_pcm_uframes_t period = req.block_frames;
    int               period_dir = 0;
    rc = snd_pcm_hw_params_set_period_size_near(be->pcm, hw, &period, &period_dir);
    if (!mel_audio__alsa_check(rc, "snd_pcm_hw_params_set_period_size_near"))
        goto fail;

    assert(req.ring_blocks > 0u);
    unsigned int periods = req.ring_blocks;
    int          periods_dir = 0;
    rc = snd_pcm_hw_params_set_periods_near(be->pcm, hw, &periods, &periods_dir);
    if (!mel_audio__alsa_check(rc, "snd_pcm_hw_params_set_periods_near"))
        goto fail;

    rc = snd_pcm_hw_params(be->pcm, hw);
    if (!mel_audio__alsa_check(rc, "snd_pcm_hw_params(commit)"))
        goto fail;

    snd_pcm_uframes_t granted_period = 0;
    rc = snd_pcm_hw_params_get_period_size(hw, &granted_period, &period_dir);
    if (!mel_audio__alsa_check(rc, "snd_pcm_hw_params_get_period_size"))
        goto fail;

    snd_pcm_uframes_t granted_buffer = 0;
    rc = snd_pcm_hw_params_get_buffer_size(hw, &granted_buffer);
    if (!mel_audio__alsa_check(rc, "snd_pcm_hw_params_get_buffer_size"))
        goto fail;

    if (channels != req.channels)
        mel_log_warn("audio.alsa", "channel count granted %u, requested %u", channels, req.channels);
    if (rate != req.samplerate)
        mel_log_warn("audio.alsa", "samplerate granted %uHz, requested %uHz", rate, req.samplerate);
    if ((u32)granted_period != req.block_frames)
        mel_log_warn("audio.alsa", "period granted %u frames, requested %u", (u32)granted_period, req.block_frames);

    be->block_frames = (u32)granted_period;
    be->channels = channels;

    u32 ring_blocks = granted_period > 0u ? (u32)(granted_buffer / granted_period) : 0u;
    if (ring_blocks < 2u)
    {
        mel_log_warn("audio.alsa", "device granted %u period(s), flooring to 2 (requested %u)", ring_blocks, req.ring_blocks);
        ring_blocks = 2u;
    }
    else if (ring_blocks != req.ring_blocks)
        mel_log_warn("audio.alsa", "ring granted %u period(s), requested %u", ring_blocks, req.ring_blocks);

    granted->samplerate = rate;
    granted->channels = channels;
    granted->block_frames = (u32)granted_period;
    granted->ring_blocks = ring_blocks;
    granted->latency_frames = (u32)granted_buffer;

    mel_log_info("audio.alsa", "device opened: %uHz %uch period %u buffer %u", rate, channels, (u32)granted_period, (u32)granted_buffer);
    return true;

fail:
    snd_pcm_close(be->pcm);
    be->pcm = NULL;
    return false;
}

static int mel_audio__alsa_writer(void* user)
{
    Mel_Alsa_Backend* be = (Mel_Alsa_Backend*)user;
    u32               block = be->block_frames;
    u32               channels = be->channels;
    u32               block_samples = block * channels;

    while (atomic_load_explicit(&be->running, memory_order_acquire) != 0u)
    {
        mel_audio_ring_read(be->ring, be->staging, block_samples);

        snd_pcm_uframes_t remaining = block;
        const f32*        cursor = be->staging;
        while (remaining > 0u)
        {
            snd_pcm_sframes_t written = snd_pcm_writei(be->pcm, cursor, remaining);
            if (written < 0)
            {
                int rc = snd_pcm_recover(be->pcm, (int)written, 1);
                if (rc < 0)
                {
                    mel_log_error("audio.alsa", "snd_pcm_writei unrecoverable: %s", snd_strerror((int)written));
                    return 1;
                }
                atomic_fetch_add_explicit(&be->xruns, 1u, memory_order_relaxed);
                continue;
            }
            remaining -= (snd_pcm_uframes_t)written;
            cursor += (usize)written * channels;
        }
    }

    return 0;
}

void mel_audio_backend_start(Mel_Audio_Ring* ring)
{
    assert(ring != NULL);

    Mel_Alsa_Backend* be = &mel_audio__alsa;
    assert(be->pcm != NULL);

    be->ring = ring;
    be->staging_samples = be->block_frames * be->channels;
    be->staging = mel_calloc(be->alloc, sizeof(f32) * (usize)be->staging_samples);
    assert(be->staging != NULL);

    int rc = snd_pcm_prepare(be->pcm);
    if (!mel_audio__alsa_check(rc, "snd_pcm_prepare"))
    {
        mel_dealloc(be->alloc, be->staging);
        be->staging = NULL;
        return;
    }

    atomic_store_explicit(&be->running, 1u, memory_order_release);
    be->spawned = mel_thread_spawn(&be->writer, mel_audio__alsa_writer, be, .name = "mel-audio-alsa") ? 1u : 0u;
    assert(be->spawned != 0u);
}

void mel_audio_backend_stop(void)
{
    Mel_Alsa_Backend* be = &mel_audio__alsa;

    if (atomic_exchange_explicit(&be->running, 0u, memory_order_acq_rel) != 0u && be->spawned != 0u)
    {
        mel_thread_join(&be->writer, NULL);
        be->spawned = 0u;
    }

    if (be->pcm != NULL)
        snd_pcm_drop(be->pcm);

    u64 xruns = atomic_load_explicit(&be->xruns, memory_order_relaxed);
    if (xruns > 0u)
        mel_log_info("audio.alsa", "device stopped after %llu xrun(s)", (unsigned long long)xruns);
}

void mel_audio_backend_close(const Mel_Alloc* a)
{
    assert(a != NULL);

    Mel_Alsa_Backend* be = &mel_audio__alsa;

    if (be->pcm != NULL)
    {
        snd_pcm_close(be->pcm);
        be->pcm = NULL;
    }

    if (be->staging != NULL)
    {
        mel_dealloc(be->alloc, be->staging);
        be->staging = NULL;
    }

    be->ring = NULL;
}

void mel_audio_backend_set_device_event(Mel_Event* ev)
{
    static u32 announced = 0u;
    if (ev != NULL && announced == 0u)
    {
        announced = 1u;
        mel_log_info("audio.alsa", "device-change events unavailable: the ALSA 'default' PCM exposes no portable hotplug notification (PipeWire would); this is a capability boundary, not a missing feature");
    }
}
