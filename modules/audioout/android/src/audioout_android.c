#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include "audioout_android_internal.h"

#include <audioout/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <thread/thread.h>
#include <log/log.h>

#include <aaudio/AAudio.h>

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#define MEL_AUDIOOUT_ANDROID_DEFAULT_SAMPLERATE        48000u
#define MEL_AUDIOOUT_ANDROID_DEFAULT_CHANNELS          2u

#define MEL_AUDIOOUT_ANDROID_TYPE_BUILTIN_EARPIECE     1
#define MEL_AUDIOOUT_ANDROID_TYPE_BUILTIN_SPEAKER      2
#define MEL_AUDIOOUT_ANDROID_TYPE_BLUETOOTH_SCO        7
#define MEL_AUDIOOUT_ANDROID_TYPE_BLUETOOTH_A2DP       8
#define MEL_AUDIOOUT_ANDROID_TYPE_HDMI                 9
#define MEL_AUDIOOUT_ANDROID_TYPE_HDMI_ARC             10
#define MEL_AUDIOOUT_ANDROID_TYPE_USB_DEVICE           11
#define MEL_AUDIOOUT_ANDROID_TYPE_USB_ACCESSORY        12
#define MEL_AUDIOOUT_ANDROID_TYPE_USB_HEADSET          22
#define MEL_AUDIOOUT_ANDROID_TYPE_BUILTIN_SPEAKER_SAFE 24
#define MEL_AUDIOOUT_ANDROID_TYPE_REMOTE_SUBMIX        25
#define MEL_AUDIOOUT_ANDROID_TYPE_BLE_HEADSET          26
#define MEL_AUDIOOUT_ANDROID_TYPE_BLE_SPEAKER          27
#define MEL_AUDIOOUT_ANDROID_TYPE_HDMI_EARC            29
#define MEL_AUDIOOUT_ANDROID_TYPE_BLE_BROADCAST        30

typedef struct
{
    Mel_AudioOut_Source src;
    bool                started;
} AOut_Open;

typedef struct
{
    u32       count;
    AOut_Open opens[];
} Open_List;

typedef struct
{
    i32                      device_id;
    i32                      type;
    str8                     stable_id;
    str8                     name;
    const mel_audioout_kind* kind;
    u32                      channels;
    u32                      samplerate;
    Mel_AudioOut_Rates       rates;
} AOut_Device;

typedef struct
{
    str8            stable_id;
    i32             device_id;
    AAudioStream*   stream;
    u32             channels;
    u32             samplerate;
    u32             burst_frames;
    f32*            scratch;
    u32             scratch_frames;
    pthread_mutex_t lock;
    _Atomic(void*)  opens;
    Mel_Array(void*) garbage;
    Mel_AudioOut_Open_Opt opt;
    Mel_AudioOut_Granted  granted;
    bool                  running;
    bool                  lost_handled;
    bool                  closing;
    bool                  reaper_spawned;
    _Atomic(u32)          lost;
    Mel_Thread            reaper;
} AOut_Stream;

typedef struct
{
    bool                  registered;
    bool                  hotplug;
    const Mel_Alloc*      alloc;
    Mel_AudioOut_Provider provider;
    Mel_Array(AOut_Device) devices;
    Mel_Array(AOut_Stream*) streams;
} AOut;

static AOut g_aout;

static const mel_audioout_kind* aout_kind_from_type(i32 type)
{
    if (type == MEL_AUDIOOUT_ANDROID_TYPE_BUILTIN_SPEAKER || type == MEL_AUDIOOUT_ANDROID_TYPE_BUILTIN_EARPIECE || type == MEL_AUDIOOUT_ANDROID_TYPE_BUILTIN_SPEAKER_SAFE)
        return &mel_audioout_builtin;
    if (type == MEL_AUDIOOUT_ANDROID_TYPE_HDMI || type == MEL_AUDIOOUT_ANDROID_TYPE_HDMI_ARC || type == MEL_AUDIOOUT_ANDROID_TYPE_HDMI_EARC)
        return &mel_audioout_hdmi;
    if (type == MEL_AUDIOOUT_ANDROID_TYPE_USB_DEVICE || type == MEL_AUDIOOUT_ANDROID_TYPE_USB_ACCESSORY || type == MEL_AUDIOOUT_ANDROID_TYPE_USB_HEADSET)
        return &mel_audioout_usb;
    if (type == MEL_AUDIOOUT_ANDROID_TYPE_BLUETOOTH_SCO || type == MEL_AUDIOOUT_ANDROID_TYPE_BLUETOOTH_A2DP || type == MEL_AUDIOOUT_ANDROID_TYPE_BLE_HEADSET || type == MEL_AUDIOOUT_ANDROID_TYPE_BLE_SPEAKER ||
        type == MEL_AUDIOOUT_ANDROID_TYPE_BLE_BROADCAST)
        return &mel_audioout_bluetooth;
    if (type == MEL_AUDIOOUT_ANDROID_TYPE_REMOTE_SUBMIX)
        return &mel_audioout_virtual;
    return &mel_audioout_unknown;
}

static u32 aout_pick_channels(const Mel_AudioOut_Rates* counts)
{
    u32 best = 0;
    for (usize i = 0; i < counts->count; i++)
        if (counts->items[i] > best)
            best = counts->items[i];
    return best > 0 ? best : MEL_AUDIOOUT_ANDROID_DEFAULT_CHANNELS;
}

static u32 aout_pick_samplerate(const Mel_AudioOut_Rates* rates)
{
    u32 best = 0;
    for (usize i = 0; i < rates->count; i++)
    {
        if (rates->items[i] == MEL_AUDIOOUT_ANDROID_DEFAULT_SAMPLERATE)
            return rates->items[i];
        if (rates->items[i] > best)
            best = rates->items[i];
    }
    return best > 0 ? best : MEL_AUDIOOUT_ANDROID_DEFAULT_SAMPLERATE;
}

static void aout_devices_clear(void)
{
    for (usize i = 0; i < g_aout.devices.count; i++)
    {
        AOut_Device* d = &g_aout.devices.items[i];
        if (d->stable_id.data)
            mel_dealloc(g_aout.alloc, d->stable_id.data);
        if (d->name.data)
            mel_dealloc(g_aout.alloc, d->name.data);
        mel_array_free(&d->rates);
    }
    mel_array_clear(&g_aout.devices);
}

static void aout_refresh_devices(void)
{
    Mel_AudioOut_Android_Devices raw;
    mel_array_init(&raw, g_aout.alloc);
    if (!mel_audioout_android__jni_enumerate(g_aout.alloc, &raw))
    {
        mel_audioout_android__jni_devices_free(&raw, g_aout.alloc);
        return;
    }

    aout_devices_clear();
    for (usize i = 0; i < raw.count; i++)
    {
        Mel_AudioOut_Android_Device* r = &raw.items[i];

        AOut_Device d;
        memset(&d, 0, sizeof d);
        d.device_id = r->id;
        d.type = r->type;
        d.kind = aout_kind_from_type(r->type);
        if (r->address.len > 0)
            d.stable_id = str8_fmt(g_aout.alloc, "android:%d:%.*s", r->type, (int)r->address.len, r->address.data);
        else
            d.stable_id = str8_fmt(g_aout.alloc, "android:%d", r->id);
        if (r->name.len > 0)
        {
            d.name = r->name;
            r->name = STR8_EMPTY;
        }
        else
            d.name = str8_fmt(g_aout.alloc, "output %d", r->id);
        mel_array_init(&d.rates, g_aout.alloc);
        for (usize j = 0; j < r->sample_rates.count; j++)
            mel_array_push(&d.rates, r->sample_rates.items[j]);
        if (d.rates.count == 0)
            mel_array_push(&d.rates, MEL_AUDIOOUT_ANDROID_DEFAULT_SAMPLERATE);
        d.samplerate = aout_pick_samplerate(&d.rates);
        d.channels = aout_pick_channels(&r->channel_counts);

        mel_array_push(&g_aout.devices, d);
    }
    mel_audioout_android__jni_devices_free(&raw, g_aout.alloc);
}

static AOut_Device* aout_device_find(str8 stable_id)
{
    for (usize i = 0; i < g_aout.devices.count; i++)
        if (str8_equals(g_aout.devices.items[i].stable_id, stable_id))
            return &g_aout.devices.items[i];
    return NULL;
}

static AOut_Stream* aout_stream_find(str8 stable_id)
{
    for (usize i = 0; i < g_aout.streams.count; i++)
    {
        AOut_Stream* st = g_aout.streams.items[i];
        if (!atomic_load_explicit(&st->lost, memory_order_acquire) && str8_equals(st->stable_id, stable_id))
            return st;
    }
    return NULL;
}

static void aout_stream_destroy(AOut_Stream* st)
{
    Open_List* ol = atomic_exchange_explicit(&st->opens, NULL, memory_order_acq_rel);
    if (ol)
        mel_dealloc(g_aout.alloc, ol);
    for (usize i = 0; i < st->garbage.count; i++)
        mel_dealloc(g_aout.alloc, st->garbage.items[i]);
    mel_array_free(&st->garbage);
    if (st->scratch)
        mel_dealloc(g_aout.alloc, st->scratch);
    if (st->stable_id.data)
        mel_dealloc(g_aout.alloc, st->stable_id.data);
    pthread_mutex_destroy(&st->lock);
    mel_dealloc(g_aout.alloc, st);
}

static void aout_reap(void)
{
    for (usize i = 0; i < g_aout.streams.count;)
    {
        AOut_Stream* st = g_aout.streams.items[i];
        if (!atomic_load_explicit(&st->lost, memory_order_acquire))
        {
            i++;
            continue;
        }
        if (st->reaper_spawned)
            mel_thread_join(&st->reaper, NULL);
        else if (st->stream)
        {
            aaudio_result_t res = AAudioStream_close(st->stream);
            if (res != AAUDIO_OK)
                mel_log_error("audioout", "android: close of lost stream failed: %s", AAudio_convertResultToText(res));
        }
        mel_array_remove_unordered(&g_aout.streams, i);
        aout_stream_destroy(st);
    }
}

static aaudio_data_callback_result_t aout_stream_on_data(AAudioStream* stream, void* user, void* audio_data, int32_t num_frames)
{
    MEL_UNUSED(stream);
    AOut_Stream* st = (AOut_Stream*)user;
    if (num_frames <= 0)
        return AAUDIO_CALLBACK_RESULT_CONTINUE;

    f32* out = (f32*)audio_data;
    u32  frames = (u32)num_frames;
    u32  channels = st->channels;
    memset(out, 0, sizeof(f32) * (usize)frames * channels);

    Open_List* ol = atomic_load_explicit(&st->opens, memory_order_acquire);
    if (!ol)
        return AAUDIO_CALLBACK_RESULT_CONTINUE;

    for (u32 i = 0; i < ol->count; i++)
    {
        if (!ol->opens[i].started)
            continue;
        u32 done = 0;
        while (done < frames)
        {
            u32 chunk = frames - done;
            if (chunk > st->scratch_frames)
                chunk = st->scratch_frames;
            u32 got = ol->opens[i].src.pull(ol->opens[i].src.token, st->scratch, chunk);
            if (got > chunk)
                got = chunk;
            f32* dst = out + (usize)done * channels;
            for (usize sample = 0; sample < (usize)got * channels; sample++)
                dst[sample] += st->scratch[sample];
            if (got < chunk)
                break;
            done += chunk;
        }
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static int aout_stream_close_main(void* user)
{
    AOut_Stream*    st = (AOut_Stream*)user;
    aaudio_result_t res = AAudioStream_close(st->stream);
    if (res != AAUDIO_OK)
        mel_log_error("audioout", "android: close of disconnected stream failed: %s", AAudio_convertResultToText(res));
    return 0;
}

static void aout_stream_on_error(AAudioStream* stream, void* user, aaudio_result_t error)
{
    MEL_UNUSED(stream);
    AOut_Stream* st = (AOut_Stream*)user;
    mel_log_error("audioout", "android: output stream error on %.*s: %s", (int)st->stable_id.len, st->stable_id.data, AAudio_convertResultToText(error));
    if (error != AAUDIO_ERROR_DISCONNECTED)
        return;

    pthread_mutex_lock(&st->lock);
    bool       first = !st->lost_handled && !st->closing;
    Open_List* ol = NULL;
    st->lost_handled = true;
    if (first)
    {
        ol = atomic_exchange_explicit(&st->opens, NULL, memory_order_acq_rel);
        if (ol)
            mel_array_push(&st->garbage, ol);
    }
    pthread_mutex_unlock(&st->lock);

    if (!first)
        return;
    if (ol)
        for (u32 i = 0; i < ol->count; i++)
            if (ol->opens[i].src.on_lost)
                ol->opens[i].src.on_lost(ol->opens[i].src.token);
    st->reaper_spawned = mel_thread_spawn(&st->reaper, aout_stream_close_main, st, .name = "audioout-close");
    if (!st->reaper_spawned)
        mel_log_error("audioout", "android: failed to spawn close thread; stream closes at next provider call");
    atomic_store_explicit(&st->lost, 1u, memory_order_release);
}

static Open_List* aout_opens_clone(AOut_Stream* st, u32 extra)
{
    Open_List* cur = atomic_load_explicit(&st->opens, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    Open_List* nl = mel_alloc(g_aout.alloc, sizeof(Open_List) + sizeof(AOut_Open) * ((usize)count + extra));
    if (!nl)
        return NULL;
    for (u32 i = 0; i < count; i++)
        nl->opens[i] = cur->opens[i];
    nl->count = count;
    return nl;
}

static void aout_opens_swap(AOut_Stream* st, Open_List* nl)
{
    Open_List* cur = atomic_exchange_explicit(&st->opens, nl, memory_order_acq_rel);
    if (cur)
        mel_array_push(&st->garbage, cur);
}

static bool aout_open_add(AOut_Stream* st, Mel_AudioOut_Source src)
{
    pthread_mutex_lock(&st->lock);
    if (st->lost_handled || st->closing)
    {
        pthread_mutex_unlock(&st->lock);
        return false;
    }
    Open_List* nl = aout_opens_clone(st, 1);
    if (!nl)
    {
        pthread_mutex_unlock(&st->lock);
        return false;
    }
    nl->opens[nl->count] = (AOut_Open){ .src = src, .started = false };
    nl->count++;
    aout_opens_swap(st, nl);
    pthread_mutex_unlock(&st->lock);
    return true;
}

static Mel_AudioOut_Status aout_status_from_aaudio(aaudio_result_t res)
{
    if (res == AAUDIO_ERROR_NO_FREE_HANDLES || res == AAUDIO_ERROR_UNAVAILABLE)
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_BUSY;
    if (res == AAUDIO_ERROR_DISCONNECTED || res == AAUDIO_ERROR_INVALID_HANDLE)
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_LOST;
    return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
}

static AOut_Stream* aout_stream_open(const AOut_Device* dev, Mel_AudioOut_Open_Opt opt, Mel_AudioOut_Status* why)
{
    *why = MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    AOut_Stream* st = mel_alloc_type(g_aout.alloc, AOut_Stream);
    if (!st)
        return NULL;
    memset(st, 0, sizeof *st);
    if (pthread_mutex_init(&st->lock, NULL) != 0)
    {
        mel_log_error("audioout", "android: stream mutex init failed");
        mel_dealloc(g_aout.alloc, st);
        return NULL;
    }
    st->stable_id = str8_dup(dev->stable_id, g_aout.alloc);
    st->device_id = dev->device_id;
    mel_array_init(&st->garbage, g_aout.alloc);
    atomic_store_explicit(&st->opens, NULL, memory_order_relaxed);

    AAudioStreamBuilder* builder = NULL;
    aaudio_result_t      res = AAudio_createStreamBuilder(&builder);
    if (res != AAUDIO_OK || !builder)
    {
        mel_log_error("audioout", "android: createStreamBuilder failed: %s", AAudio_convertResultToText(res));
        aout_stream_destroy(st);
        return NULL;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setDeviceId(builder, dev->device_id);
    AAudioStreamBuilder_setSharingMode(builder, opt.exclusive ? AAUDIO_SHARING_MODE_EXCLUSIVE : AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setSampleRate(builder, (int32_t)dev->samplerate);
    AAudioStreamBuilder_setChannelCount(builder, (int32_t)dev->channels);
    AAudioStreamBuilder_setDataCallback(builder, aout_stream_on_data, st);
    AAudioStreamBuilder_setErrorCallback(builder, aout_stream_on_error, st);

    AAudioStream* stream = NULL;
    res = AAudioStreamBuilder_openStream(builder, &stream);
    if (res != AAUDIO_OK || !stream)
    {
        mel_log_warn("audioout", "android: output openStream %uHz %uch failed (%s); retrying unspecified", dev->samplerate, dev->channels, AAudio_convertResultToText(res));
        AAudioStreamBuilder_setSampleRate(builder, AAUDIO_UNSPECIFIED);
        AAudioStreamBuilder_setChannelCount(builder, AAUDIO_UNSPECIFIED);
        stream = NULL;
        res = AAudioStreamBuilder_openStream(builder, &stream);
    }
    AAudioStreamBuilder_delete(builder);

    if (res != AAUDIO_OK || !stream)
    {
        mel_log_error("audioout", "android: output openStream failed for %.*s: %s", (int)dev->stable_id.len, dev->stable_id.data, AAudio_convertResultToText(res));
        *why = aout_status_from_aaudio(res);
        aout_stream_destroy(st);
        return NULL;
    }
    if (AAudioStream_getFormat(stream) != AAUDIO_FORMAT_PCM_FLOAT)
    {
        mel_log_error("audioout", "android: output stream granted non-float format for %.*s", (int)dev->stable_id.len, dev->stable_id.data);
        AAudioStream_close(stream);
        aout_stream_destroy(st);
        return NULL;
    }

    st->stream = stream;
    st->samplerate = (u32)AAudioStream_getSampleRate(stream);
    st->channels = (u32)AAudioStream_getChannelCount(stream);
    int32_t burst = AAudioStream_getFramesPerBurst(stream);
    st->burst_frames = burst > 0 ? (u32)burst : 0;
    int32_t capacity = AAudioStream_getBufferCapacityInFrames(stream);
    st->scratch_frames = capacity > 0 ? (u32)capacity : st->burst_frames;
    if (st->samplerate == 0 || st->channels == 0 || st->scratch_frames == 0)
    {
        mel_log_error("audioout", "android: output stream reported degenerate format for %.*s (%u ch @ %u Hz, %u frames)", (int)dev->stable_id.len, dev->stable_id.data, st->channels, st->samplerate, st->scratch_frames);
        AAudioStream_close(stream);
        aout_stream_destroy(st);
        return NULL;
    }
    if (st->burst_frames == 0)
        st->burst_frames = st->scratch_frames;
    st->scratch = mel_alloc(g_aout.alloc, sizeof(f32) * (usize)st->scratch_frames * st->channels);
    if (!st->scratch)
    {
        mel_log_error("audioout", "android: scratch allocation failed for %.*s", (int)dev->stable_id.len, dev->stable_id.data);
        AAudioStream_close(stream);
        aout_stream_destroy(st);
        return NULL;
    }
    int32_t buffer_frames = AAudioStream_getBufferSizeInFrames(stream);
    st->opt = opt;
    st->granted = (Mel_AudioOut_Granted){
        .format = { .samplerate = st->samplerate, .channels = st->channels, .block_frames = st->burst_frames },
        .exclusive = AAudioStream_getSharingMode(stream) == AAUDIO_SHARING_MODE_EXCLUSIVE,
        .os_timestamps = buffer_frames > 0,
        .latency_frames = buffer_frames > 0 ? (u32)buffer_frames : st->scratch_frames,
    };
    if (opt.exclusive && !st->granted.exclusive)
        mel_log_warn("audioout", "android: exclusive requested on %.*s; granted shared", (int)dev->stable_id.len, dev->stable_id.data);
    return st;
}

static void aout_enumerate(void* user, Mel_AudioOut_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    if (!g_aout.hotplug)
        g_aout.hotplug = mel_audioout_android__hotplug_start();
    aout_refresh_devices();

    for (usize i = 0; i < g_aout.devices.count; i++)
    {
        AOut_Device*     d = &g_aout.devices.items[i];
        Mel_AudioOut_Raw raw = {
            .stable_id = d->stable_id,
            .name = d->name,
            .kind = d->kind,
            .channels = d->channels,
            .samplerate = d->samplerate,
            .samplerates = d->rates.items,
            .samplerate_count = (u32)d->rates.count,
            .caps = { .volume = false, .mute = false },
        };
        if (!fn(&raw, fn_user))
            return;
    }
}

static str8 aout_default_id(void* user)
{
    MEL_UNUSED(user);
    for (usize i = 0; i < g_aout.devices.count; i++)
        if (g_aout.devices.items[i].type == MEL_AUDIOOUT_ANDROID_TYPE_BUILTIN_SPEAKER)
            return g_aout.devices.items[i].stable_id;
    return STR8_EMPTY;
}

static Mel_AudioOut_Status aout_open(void* user, str8 stable_id, Mel_AudioOut_Format req, Mel_AudioOut_Open_Opt opt, Mel_AudioOut_Granted* granted, Mel_AudioOut_Source src)
{
    MEL_UNUSED(user);
    assert(granted != NULL);
    assert(src.pull != NULL);
    aout_reap();

    AOut_Stream* st = aout_stream_find(stable_id);
    bool         fresh = false;
    if (!st)
    {
        AOut_Device* dev = aout_device_find(stable_id);
        if (!dev)
        {
            mel_log_error("audioout", "android: open unknown device %.*s", (int)stable_id.len, stable_id.data);
            return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_NO_DEVICE;
        }
        Mel_AudioOut_Status why;
        st = aout_stream_open(dev, opt, &why);
        if (!st)
            return why;
        fresh = true;
    }
    else if (opt.exclusive != st->opt.exclusive)
        mel_log_warn("audioout", "android: open options differ from the live stream on %.*s; first open's configuration applies", (int)stable_id.len, stable_id.data);

    if (!aout_open_add(st, src))
    {
        if (fresh)
        {
            AAudioStream_close(st->stream);
            aout_stream_destroy(st);
            return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
        }
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_LOST;
    }
    if (fresh)
    {
        mel_array_push(&g_aout.streams, st);
        mel_log_info("audioout", "android: output stream opened on %.*s (%u ch @ %u Hz, burst %u, %s)", (int)stable_id.len, stable_id.data, st->channels, st->samplerate, st->burst_frames, st->granted.exclusive ? "exclusive" : "shared");
    }

    *granted = st->granted;
    if ((req.samplerate != 0 && req.samplerate != st->samplerate) || (req.channels != 0 && req.channels != st->channels))
        mel_log_info("audioout", "android: granted %uHz %uch on %.*s (requested %uHz %uch)", st->samplerate, st->channels, (int)stable_id.len, stable_id.data, req.samplerate, req.channels);
    return MEL_AUDIOOUT_OK;
}

static u32 aout_started_count(const Open_List* ol)
{
    u32 n = 0;
    if (ol)
        for (u32 i = 0; i < ol->count; i++)
            if (ol->opens[i].started)
                n++;
    return n;
}

static void aout_set_started(str8 stable_id, void* token, bool started)
{
    aout_reap();
    AOut_Stream* st = aout_stream_find(stable_id);
    if (!st)
        return;

    pthread_mutex_lock(&st->lock);
    if (st->lost_handled || st->closing)
    {
        pthread_mutex_unlock(&st->lock);
        return;
    }
    Open_List* nl = aout_opens_clone(st, 0);
    if (!nl)
    {
        pthread_mutex_unlock(&st->lock);
        return;
    }
    for (u32 i = 0; i < nl->count; i++)
        if (nl->opens[i].src.token == token)
            nl->opens[i].started = started;
    aout_opens_swap(st, nl);
    u32  active = aout_started_count(nl);
    bool want_start = active > 0 && !st->running;
    bool want_stop = active == 0 && st->running;
    st->running = active > 0;
    pthread_mutex_unlock(&st->lock);

    if (want_start)
    {
        aaudio_result_t res = AAudioStream_requestStart(st->stream);
        if (res != AAUDIO_OK)
            mel_log_error("audioout", "android: output requestStart failed for %.*s: %s", (int)stable_id.len, stable_id.data, AAudio_convertResultToText(res));
    }
    if (want_stop)
    {
        aaudio_result_t res = AAudioStream_requestStop(st->stream);
        if (res != AAUDIO_OK)
            mel_log_error("audioout", "android: output requestStop failed for %.*s: %s", (int)stable_id.len, stable_id.data, AAudio_convertResultToText(res));
    }
}

static void aout_start(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    aout_set_started(stable_id, token, true);
}

static void aout_stop(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    aout_set_started(stable_id, token, false);
}

static void aout_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    aout_reap();

    AOut_Stream* st = aout_stream_find(stable_id);
    if (!st)
        return;

    pthread_mutex_lock(&st->lock);
    if (st->lost_handled || st->closing)
    {
        pthread_mutex_unlock(&st->lock);
        return;
    }
    Open_List* cur = atomic_load_explicit(&st->opens, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    if (count == 0)
    {
        pthread_mutex_unlock(&st->lock);
        return;
    }
    Open_List* nl = mel_alloc(g_aout.alloc, sizeof(Open_List) + sizeof(AOut_Open) * (usize)count);
    if (!nl)
    {
        pthread_mutex_unlock(&st->lock);
        return;
    }
    u32 kept = 0;
    for (u32 i = 0; i < count; i++)
        if (cur->opens[i].src.token != token)
            nl->opens[kept++] = cur->opens[i];
    nl->count = kept;
    aout_opens_swap(st, nl);
    bool last = kept == 0;
    bool was_running = st->running;
    if (last)
    {
        st->closing = true;
        st->running = false;
    }
    pthread_mutex_unlock(&st->lock);

    if (!last)
        return;

    if (was_running)
    {
        aaudio_result_t res = AAudioStream_requestStop(st->stream);
        if (res != AAUDIO_OK)
            mel_log_error("audioout", "android: output requestStop failed for %.*s: %s", (int)stable_id.len, stable_id.data, AAudio_convertResultToText(res));
    }
    aaudio_result_t res = AAudioStream_close(st->stream);
    if (res != AAUDIO_OK)
        mel_log_error("audioout", "android: output close failed for %.*s: %s", (int)stable_id.len, stable_id.data, AAudio_convertResultToText(res));

    for (usize i = 0; i < g_aout.streams.count; i++)
    {
        if (g_aout.streams.items[i] == st)
        {
            mel_array_remove_unordered(&g_aout.streams, i);
            break;
        }
    }
    aout_stream_destroy(st);
}

static void* aout_native(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    AOut_Stream* st = aout_stream_find(stable_id);
    return st ? (void*)st->stream : NULL;
}

static void aout_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);

    if (g_aout.hotplug)
    {
        mel_audioout_android__hotplug_stop();
        g_aout.hotplug = false;
    }

    aout_reap();
    for (usize i = 0; i < g_aout.streams.count; i++)
    {
        AOut_Stream* st = g_aout.streams.items[i];

        pthread_mutex_lock(&st->lock);
        st->closing = true;
        st->running = false;
        Open_List* ol = atomic_exchange_explicit(&st->opens, NULL, memory_order_acq_rel);
        if (ol)
            mel_array_push(&st->garbage, ol);
        pthread_mutex_unlock(&st->lock);

        if (ol && ol->count > 0)
        {
            mel_log_warn("audioout", "android: output stream on %.*s still open at shutdown; releasing", (int)st->stable_id.len, st->stable_id.data);
            for (u32 j = 0; j < ol->count; j++)
                if (ol->opens[j].src.on_lost)
                    ol->opens[j].src.on_lost(ol->opens[j].src.token);
        }

        AAudioStream_requestStop(st->stream);
        aaudio_result_t res = AAudioStream_close(st->stream);
        if (res != AAUDIO_OK)
            mel_log_error("audioout", "android: output close failed at shutdown: %s", AAudio_convertResultToText(res));
        aout_stream_destroy(st);
    }
    mel_array_free(&g_aout.streams);

    aout_devices_clear();
    mel_array_free(&g_aout.devices);

    memset(&g_aout, 0, sizeof g_aout);
}

void mel_audioout_android__on_devices_changed(void)
{
    if (g_aout.registered)
        mel_audioout_provider_notify(g_aout.provider);
}

void mel_audioout__register_host_providers(void)
{
    static const Mel_AudioOut_Provider_Desc desc = {
        .name = "android-audiomanager",
        .enumerate = aout_enumerate,
        .default_id = aout_default_id,
        .open = aout_open,
        .start = aout_start,
        .stop = aout_stop,
        .close = aout_close,
        .native = aout_native,
        .shutdown = aout_shutdown,
    };
    g_aout.alloc = mel_alloc_heap();
    mel_array_init(&g_aout.devices, g_aout.alloc);
    mel_array_init(&g_aout.streams, g_aout.alloc);
    g_aout.provider = mel_audioout_provider_register(&desc);
    g_aout.registered = true;
}
