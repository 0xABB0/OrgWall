#include <audio/backend.h>

#include "../audio_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>
#include <event/event.h>
#include <log/log.h>

#include <aaudio/AAudio.h>

#include <stdatomic.h>
#include <string.h>

#define MEL_AUDIO__DEVICE_EVENT_DISCONNECTED 1u

typedef struct
{
    AAudioStream*       stream;
    Mel_Audio_Ring*     ring;
    u32                 channels;
    u32                 block_frames;
    _Atomic(u32)        underruns;
    u32                 started;
    _Atomic(Mel_Event*) device_events;
} Mel_Audio__AAudio;

static Mel_Audio__AAudio g_aaudio;

static aaudio_data_callback_result_t mel_audio__aaudio_data(AAudioStream* stream, void* user, void* audio_data, int32_t num_frames)
{
    MEL_UNUSED(stream);

    Mel_Audio__AAudio* st = (Mel_Audio__AAudio*)user;
    f32*               out = (f32*)audio_data;
    u32                want = (u32)num_frames * st->channels;

    u32 got = mel_audio_ring_read(st->ring, out, want);
    if (got < want)
        atomic_fetch_add_explicit(&st->underruns, 1u, memory_order_relaxed);

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void mel_audio__aaudio_error(AAudioStream* stream, void* user, aaudio_result_t error)
{
    MEL_UNUSED(stream);

    Mel_Audio__AAudio* st = (Mel_Audio__AAudio*)user;
    mel_log_error("audio", "aaudio device error: %s", AAudio_convertResultToText(error));

    if (error == AAUDIO_ERROR_DISCONNECTED)
    {
        Mel_Event* ev = atomic_load_explicit(&st->device_events, memory_order_acquire);
        if (ev != NULL)
        {
            u32 code = MEL_AUDIO__DEVICE_EVENT_DISCONNECTED;
            mel_event_fire(ev, &code);
        }
    }
}

bool mel_audio_backend_open(Mel_Audio_Opt req, Mel_Audio_Caps* granted, const Mel_Alloc* a)
{
    assert(granted != NULL);
    assert(a != NULL);
    assert(req.samplerate > 0u);
    assert(req.channels >= 1u);
    assert(req.block_frames > 0u);

    MEL_UNUSED(a);

    Mel_Event* prior_events = atomic_load_explicit(&g_aaudio.device_events, memory_order_relaxed);
    g_aaudio = (Mel_Audio__AAudio){ 0 };
    atomic_store_explicit(&g_aaudio.device_events, prior_events, memory_order_relaxed);

    AAudioStreamBuilder* builder = NULL;
    aaudio_result_t      res = AAudio_createStreamBuilder(&builder);
    if (res != AAUDIO_OK || builder == NULL)
    {
        mel_log_error("audio", "aaudio createStreamBuilder failed: %s", AAudio_convertResultToText(res));
        return false;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setSampleRate(builder, (int32_t)req.samplerate);
    AAudioStreamBuilder_setChannelCount(builder, (int32_t)req.channels);
    AAudioStreamBuilder_setDataCallback(builder, mel_audio__aaudio_data, &g_aaudio);
    AAudioStreamBuilder_setErrorCallback(builder, mel_audio__aaudio_error, &g_aaudio);

    AAudioStream* stream = NULL;
    res = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);

    if (res != AAUDIO_OK || stream == NULL)
    {
        mel_log_error("audio", "aaudio openStream failed: %s (requested %uHz %uch)", AAudio_convertResultToText(res), req.samplerate, req.channels);
        return false;
    }

    aaudio_format_t format = AAudioStream_getFormat(stream);
    if (format != AAUDIO_FORMAT_PCM_FLOAT)
    {
        mel_log_error("audio", "aaudio granted non-float format %d, PCM_FLOAT required", (int)format);
        AAudioStream_close(stream);
        return false;
    }

    u32 grant_rate = (u32)AAudioStream_getSampleRate(stream);
    u32 grant_channels = (u32)AAudioStream_getChannelCount(stream);
    u32 burst = (u32)AAudioStream_getFramesPerBurst(stream);

    assert(grant_rate > 0u);
    assert(grant_channels >= 1u);

    u32 block = burst > 0u ? burst : req.block_frames;
    assert(req.ring_blocks > 0u);
    u32 ring_blocks = req.ring_blocks;

    if (grant_rate != req.samplerate || grant_channels != req.channels)
        mel_log_warn("audio", "aaudio granted %uHz %uch (requested %uHz %uch)", grant_rate, grant_channels, req.samplerate, req.channels);

    g_aaudio.stream = stream;
    g_aaudio.channels = grant_channels;
    g_aaudio.block_frames = block;
    atomic_store_explicit(&g_aaudio.underruns, 0u, memory_order_relaxed);

    *granted = (Mel_Audio_Caps){
        .samplerate = grant_rate,
        .channels = grant_channels,
        .block_frames = block,
        .ring_blocks = ring_blocks,
        .latency_frames = ring_blocks * block,
    };

    mel_log_info("audio", "aaudio opened %uHz %uch block %u ring %u", grant_rate, grant_channels, block, ring_blocks);
    return true;
}

void mel_audio_backend_start(Mel_Audio_Ring* ring)
{
    assert(ring != NULL);
    assert(g_aaudio.stream != NULL);

    g_aaudio.ring = ring;

    aaudio_result_t res = AAudioStream_requestStart(g_aaudio.stream);
    if (res != AAUDIO_OK)
    {
        mel_log_error("audio", "aaudio requestStart failed: %s", AAudio_convertResultToText(res));
        return;
    }
    g_aaudio.started = 1u;
}

void mel_audio_backend_stop(void)
{
    if (g_aaudio.stream == NULL || g_aaudio.started == 0u)
        return;

    aaudio_result_t res = AAudioStream_requestStop(g_aaudio.stream);
    if (res != AAUDIO_OK)
        mel_log_error("audio", "aaudio requestStop failed: %s", AAudio_convertResultToText(res));

    g_aaudio.started = 0u;
}

void mel_audio_backend_close(const Mel_Alloc* a)
{
    MEL_UNUSED(a);

    if (g_aaudio.stream != NULL)
    {
        aaudio_result_t res = AAudioStream_close(g_aaudio.stream);
        if (res != AAUDIO_OK)
            mel_log_error("audio", "aaudio close failed: %s", AAudio_convertResultToText(res));
    }

    u32 underruns = atomic_load_explicit(&g_aaudio.underruns, memory_order_relaxed);
    if (underruns > 0u)
        mel_log_warn("audio", "aaudio closed with %u underruns", underruns);

    Mel_Event* prior_events = atomic_load_explicit(&g_aaudio.device_events, memory_order_relaxed);
    g_aaudio = (Mel_Audio__AAudio){ 0 };
    atomic_store_explicit(&g_aaudio.device_events, prior_events, memory_order_relaxed);
}

void mel_audio_backend_set_device_event(Mel_Event* ev)
{
    atomic_store_explicit(&g_aaudio.device_events, ev, memory_order_release);
}
