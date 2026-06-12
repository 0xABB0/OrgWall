#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include "audioin_android_internal.h"

#include <audioin/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <thread/thread.h>
#include <platform/android/jni.h>
#include <log/log.h>

#include <aaudio/AAudio.h>

#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#define MEL_AUDIOIN_ANDROID_PERMISSION_REQUEST_CODE 0x4D41
#define MEL_AUDIOIN_ANDROID_DEFAULT_SAMPLERATE      48000u

#define MEL_AUDIOIN_ANDROID_TYPE_BLUETOOTH_SCO      7
#define MEL_AUDIOIN_ANDROID_TYPE_USB_DEVICE         11
#define MEL_AUDIOIN_ANDROID_TYPE_BUILTIN_MIC        15
#define MEL_AUDIOIN_ANDROID_TYPE_USB_HEADSET        22
#define MEL_AUDIOIN_ANDROID_TYPE_REMOTE_SUBMIX      25
#define MEL_AUDIOIN_ANDROID_TYPE_BLE_HEADSET        26

typedef struct
{
    u32              count;
    Mel_AudioIn_Sink sinks[];
} Sink_List;

typedef struct
{
    i32                     device_id;
    str8                    stable_id;
    str8                    name;
    const mel_audioin_kind* kind;
    u32                     channels;
    u32                     samplerate;
    Mel_AudioIn_Rates       rates;
} AIn_Device;

typedef struct
{
    str8                 stable_id;
    i32                  device_id;
    AAudioStream*        stream;
    u32                  channels;
    u32                  samplerate;
    u64                  frames_delivered;
    Mel_AudioIn_Open_Opt opt;
    Mel_AudioIn_Granted  granted;
    pthread_mutex_t      lock;
    _Atomic(void*)       sinks;
    Mel_Array(void*) garbage;
    bool         lost_handled;
    bool         closing;
    bool         reaper_spawned;
    _Atomic(u32) lost;
    Mel_Thread   reaper;
} AIn_Stream;

typedef Mel_Array(Mel_AudioIn_Sink) AIn_Sink_Array;

typedef struct
{
    bool                 registered;
    bool                 hotplug;
    bool                 permission_listening;
    bool                 answered_denied;
    const Mel_Alloc*     alloc;
    Mel_AudioIn_Provider provider;
    Mel_Array(AIn_Device) devices;
    Mel_Array(AIn_Stream*) streams;
    AIn_Sink_Array auth_sinks;
} AIn;

static AIn             g_ain;
static pthread_mutex_t g_auth_lock = PTHREAD_MUTEX_INITIALIZER;

static const mel_audioin_kind* ain_kind_from_type(i32 type)
{
    if (type == MEL_AUDIOIN_ANDROID_TYPE_BUILTIN_MIC)
        return &mel_audioin_builtin;
    if (type == MEL_AUDIOIN_ANDROID_TYPE_USB_DEVICE || type == MEL_AUDIOIN_ANDROID_TYPE_USB_HEADSET)
        return &mel_audioin_usb;
    if (type == MEL_AUDIOIN_ANDROID_TYPE_BLUETOOTH_SCO || type == MEL_AUDIOIN_ANDROID_TYPE_BLE_HEADSET)
        return &mel_audioin_bluetooth;
    if (type == MEL_AUDIOIN_ANDROID_TYPE_REMOTE_SUBMIX)
        return &mel_audioin_loopback;
    return &mel_audioin_unknown;
}

static u32 ain_pick_channels(const Mel_AudioIn_Rates* counts)
{
    u32 best = 0;
    for (usize i = 0; i < counts->count; i++)
        if (counts->items[i] > best)
            best = counts->items[i];
    return best > 0 ? best : 1;
}

static u32 ain_pick_samplerate(const Mel_AudioIn_Rates* rates)
{
    u32 best = 0;
    for (usize i = 0; i < rates->count; i++)
    {
        if (rates->items[i] == MEL_AUDIOIN_ANDROID_DEFAULT_SAMPLERATE)
            return rates->items[i];
        if (rates->items[i] > best)
            best = rates->items[i];
    }
    return best > 0 ? best : MEL_AUDIOIN_ANDROID_DEFAULT_SAMPLERATE;
}

static void ain_devices_clear(void)
{
    for (usize i = 0; i < g_ain.devices.count; i++)
    {
        AIn_Device* d = &g_ain.devices.items[i];
        if (d->stable_id.data)
            mel_dealloc(g_ain.alloc, d->stable_id.data);
        if (d->name.data)
            mel_dealloc(g_ain.alloc, d->name.data);
        mel_array_free(&d->rates);
    }
    mel_array_clear(&g_ain.devices);
}

static void ain_refresh_devices(void)
{
    Mel_AudioIn_Android_Devices raw;
    mel_array_init(&raw, g_ain.alloc);
    if (!mel_audioin_android__jni_enumerate(g_ain.alloc, &raw))
    {
        mel_audioin_android__jni_devices_free(&raw, g_ain.alloc);
        return;
    }

    ain_devices_clear();
    for (usize i = 0; i < raw.count; i++)
    {
        Mel_AudioIn_Android_Device* r = &raw.items[i];

        AIn_Device d;
        memset(&d, 0, sizeof d);
        d.device_id = r->id;
        d.kind = ain_kind_from_type(r->type);
        if (r->address.len > 0)
            d.stable_id = str8_fmt(g_ain.alloc, "android:%d:%.*s", r->type, (int)r->address.len, r->address.data);
        else
            d.stable_id = str8_fmt(g_ain.alloc, "android:%d", r->id);
        if (r->name.len > 0)
        {
            d.name = r->name;
            r->name = STR8_EMPTY;
        }
        else
            d.name = str8_fmt(g_ain.alloc, "input %d", r->id);
        mel_array_init(&d.rates, g_ain.alloc);
        for (usize j = 0; j < r->sample_rates.count; j++)
            mel_array_push(&d.rates, r->sample_rates.items[j]);
        if (d.rates.count == 0)
            mel_array_push(&d.rates, MEL_AUDIOIN_ANDROID_DEFAULT_SAMPLERATE);
        d.samplerate = ain_pick_samplerate(&d.rates);
        d.channels = ain_pick_channels(&r->channel_counts);

        mel_array_push(&g_ain.devices, d);
    }
    mel_audioin_android__jni_devices_free(&raw, g_ain.alloc);
}

static AIn_Device* ain_device_find(str8 stable_id)
{
    for (usize i = 0; i < g_ain.devices.count; i++)
        if (str8_equals(g_ain.devices.items[i].stable_id, stable_id))
            return &g_ain.devices.items[i];
    return NULL;
}

static AIn_Stream* ain_stream_find(str8 stable_id)
{
    for (usize i = 0; i < g_ain.streams.count; i++)
    {
        AIn_Stream* st = g_ain.streams.items[i];
        if (!atomic_load_explicit(&st->lost, memory_order_acquire) && str8_equals(st->stable_id, stable_id))
            return st;
    }
    return NULL;
}

static void ain_stream_destroy(AIn_Stream* st)
{
    Sink_List* sl = atomic_exchange_explicit(&st->sinks, NULL, memory_order_acq_rel);
    if (sl)
        mel_dealloc(g_ain.alloc, sl);
    for (usize i = 0; i < st->garbage.count; i++)
        mel_dealloc(g_ain.alloc, st->garbage.items[i]);
    mel_array_free(&st->garbage);
    if (st->stable_id.data)
        mel_dealloc(g_ain.alloc, st->stable_id.data);
    pthread_mutex_destroy(&st->lock);
    mel_dealloc(g_ain.alloc, st);
}

static void ain_reap(void)
{
    for (usize i = 0; i < g_ain.streams.count;)
    {
        AIn_Stream* st = g_ain.streams.items[i];
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
                mel_log_error("audioin", "android: close of lost stream failed: %s", AAudio_convertResultToText(res));
        }
        mel_array_remove_unordered(&g_ain.streams, i);
        ain_stream_destroy(st);
    }
}

static aaudio_data_callback_result_t ain_stream_on_data(AAudioStream* stream, void* user, void* audio_data, int32_t num_frames)
{
    AIn_Stream* st = (AIn_Stream*)user;
    if (num_frames <= 0)
        return AAUDIO_CALLBACK_RESULT_CONTINUE;

    u64     ts = 0;
    int64_t fpos = 0;
    int64_t tns = 0;
    if (st->samplerate > 0 && AAudioStream_getTimestamp(stream, CLOCK_MONOTONIC, &fpos, &tns) == AAUDIO_OK)
    {
        i64 t = tns + (((i64)st->frames_delivered - fpos) * 1000000000ll) / (i64)st->samplerate;
        if (t > 0)
            ts = (u64)t;
    }

    Sink_List* sl = atomic_load_explicit(&st->sinks, memory_order_acquire);
    if (sl)
        for (u32 i = 0; i < sl->count; i++)
            if (sl->sinks[i].on_frames)
                sl->sinks[i].on_frames(sl->sinks[i].token, (const f32*)audio_data, (u32)num_frames, st->samplerate, st->channels, ts);
    st->frames_delivered += (u64)num_frames;
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static int ain_stream_close_main(void* user)
{
    AIn_Stream*     st = (AIn_Stream*)user;
    aaudio_result_t res = AAudioStream_close(st->stream);
    if (res != AAUDIO_OK)
        mel_log_error("audioin", "android: close of disconnected stream failed: %s", AAudio_convertResultToText(res));
    return 0;
}

static void ain_stream_on_error(AAudioStream* stream, void* user, aaudio_result_t error)
{
    MEL_UNUSED(stream);
    AIn_Stream* st = (AIn_Stream*)user;
    mel_log_error("audioin", "android: input stream error on %.*s: %s", (int)st->stable_id.len, st->stable_id.data, AAudio_convertResultToText(error));
    if (error != AAUDIO_ERROR_DISCONNECTED)
        return;

    pthread_mutex_lock(&st->lock);
    bool first = !st->lost_handled && !st->closing;
    st->lost_handled = true;
    Sink_List* sl = NULL;
    if (first)
    {
        sl = atomic_exchange_explicit(&st->sinks, NULL, memory_order_acq_rel);
        if (sl)
            mel_array_push(&st->garbage, sl);
    }
    pthread_mutex_unlock(&st->lock);

    if (!first)
        return;
    if (sl)
        for (u32 i = 0; i < sl->count; i++)
            if (sl->sinks[i].on_lost)
                sl->sinks[i].on_lost(sl->sinks[i].token);
    st->reaper_spawned = mel_thread_spawn(&st->reaper, ain_stream_close_main, st, .name = "audioin-close");
    if (!st->reaper_spawned)
        mel_log_error("audioin", "android: failed to spawn close thread; stream closes at next provider call");
    atomic_store_explicit(&st->lost, 1u, memory_order_release);
}

static bool ain_sink_add(AIn_Stream* st, Mel_AudioIn_Sink sink)
{
    pthread_mutex_lock(&st->lock);
    if (st->lost_handled || st->closing)
    {
        pthread_mutex_unlock(&st->lock);
        return false;
    }
    Sink_List* cur = atomic_load_explicit(&st->sinks, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    Sink_List* nl = mel_alloc(g_ain.alloc, sizeof(Sink_List) + sizeof(Mel_AudioIn_Sink) * ((usize)count + 1u));
    if (!nl)
    {
        pthread_mutex_unlock(&st->lock);
        return false;
    }
    for (u32 i = 0; i < count; i++)
        nl->sinks[i] = cur->sinks[i];
    nl->sinks[count] = sink;
    nl->count = count + 1u;
    atomic_store_explicit(&st->sinks, nl, memory_order_release);
    if (cur)
        mel_array_push(&st->garbage, cur);
    pthread_mutex_unlock(&st->lock);
    return true;
}

static bool ain_opt_equal(Mel_AudioIn_Open_Opt a, Mel_AudioIn_Open_Opt b)
{
    return a.processing.echo_cancellation == b.processing.echo_cancellation && a.processing.noise_suppression == b.processing.noise_suppression && a.processing.auto_gain == b.processing.auto_gain && a.exclusive == b.exclusive;
}

static bool ain_result_busy(aaudio_result_t res) { return res == AAUDIO_ERROR_NO_FREE_HANDLES || res == AAUDIO_ERROR_UNAVAILABLE; }

typedef void (*AIn_Set_Input_Preset_Fn)(AAudioStreamBuilder* builder, aaudio_input_preset_t preset);

static AIn_Set_Input_Preset_Fn ain_set_input_preset_fn(void) { return (AIn_Set_Input_Preset_Fn)dlsym(RTLD_DEFAULT, "AAudioStreamBuilder_setInputPreset"); }

static AIn_Stream* ain_stream_open(const AIn_Device* dev, Mel_AudioIn_Open_Opt opt, Mel_AudioIn_Status* fail_status)
{
    *fail_status = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    AIn_Stream* st = mel_alloc_type(g_ain.alloc, AIn_Stream);
    if (!st)
        return NULL;
    memset(st, 0, sizeof *st);
    if (pthread_mutex_init(&st->lock, NULL) != 0)
    {
        mel_log_error("audioin", "android: stream mutex init failed");
        mel_dealloc(g_ain.alloc, st);
        return NULL;
    }
    st->stable_id = str8_dup(dev->stable_id, g_ain.alloc);
    st->device_id = dev->device_id;
    mel_array_init(&st->garbage, g_ain.alloc);
    atomic_store_explicit(&st->sinks, NULL, memory_order_relaxed);

    AAudioStreamBuilder* builder = NULL;
    aaudio_result_t      res = AAudio_createStreamBuilder(&builder);
    if (res != AAUDIO_OK || !builder)
    {
        mel_log_error("audioin", "android: createStreamBuilder failed: %s", AAudio_convertResultToText(res));
        ain_stream_destroy(st);
        return NULL;
    }

    bool want_voice = opt.processing.echo_cancellation || opt.processing.noise_suppression;
    bool preset_applied = false;
    if (want_voice)
    {
        AIn_Set_Input_Preset_Fn set_preset = ain_set_input_preset_fn();
        if (set_preset)
        {
            set_preset(builder, AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION);
            preset_applied = true;
        }
        else
            mel_log_warn("audioin", "android: voice processing requested on %.*s but input presets need API 28; capturing unprocessed", (int)dev->stable_id.len, dev->stable_id.data);
    }
    if (opt.processing.auto_gain)
        mel_log_warn("audioin", "android: auto_gain requested on %.*s; no queryable AGC surface, not granted", (int)dev->stable_id.len, dev->stable_id.data);

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setDeviceId(builder, dev->device_id);
    AAudioStreamBuilder_setSharingMode(builder, opt.exclusive ? AAUDIO_SHARING_MODE_EXCLUSIVE : AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setSampleRate(builder, (int32_t)dev->samplerate);
    AAudioStreamBuilder_setChannelCount(builder, (int32_t)dev->channels);
    AAudioStreamBuilder_setDataCallback(builder, ain_stream_on_data, st);
    AAudioStreamBuilder_setErrorCallback(builder, ain_stream_on_error, st);

    AAudioStream* stream = NULL;
    res = AAudioStreamBuilder_openStream(builder, &stream);
    if (res != AAUDIO_OK || !stream)
    {
        mel_log_warn("audioin", "android: input openStream %uHz %uch failed (%s); retrying unspecified", dev->samplerate, dev->channels, AAudio_convertResultToText(res));
        AAudioStreamBuilder_setSampleRate(builder, AAUDIO_UNSPECIFIED);
        AAudioStreamBuilder_setChannelCount(builder, AAUDIO_UNSPECIFIED);
        stream = NULL;
        res = AAudioStreamBuilder_openStream(builder, &stream);
    }
    AAudioStreamBuilder_delete(builder);

    if (res != AAUDIO_OK || !stream)
    {
        mel_log_error("audioin", "android: input openStream failed for %.*s: %s", (int)dev->stable_id.len, dev->stable_id.data, AAudio_convertResultToText(res));
        if (ain_result_busy(res))
            *fail_status = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_BUSY;
        ain_stream_destroy(st);
        return NULL;
    }
    if (AAudioStream_getFormat(stream) != AAUDIO_FORMAT_PCM_FLOAT)
    {
        mel_log_error("audioin", "android: input stream granted non-float format for %.*s", (int)dev->stable_id.len, dev->stable_id.data);
        AAudioStream_close(stream);
        ain_stream_destroy(st);
        return NULL;
    }

    st->stream = stream;
    st->samplerate = (u32)AAudioStream_getSampleRate(stream);
    st->channels = (u32)AAudioStream_getChannelCount(stream);
    st->opt = opt;
    st->granted = (Mel_AudioIn_Granted){
        .processing = { .echo_cancellation = preset_applied, .noise_suppression = preset_applied, .auto_gain = false },
        .exclusive = AAudioStream_getSharingMode(stream) == AAUDIO_SHARING_MODE_EXCLUSIVE,
        .os_timestamps = true,
    };
    if (opt.exclusive && !st->granted.exclusive)
        mel_log_warn("audioin", "android: exclusive requested on %.*s; granted shared", (int)dev->stable_id.len, dev->stable_id.data);
    return st;
}

static void ain_enumerate(void* user, Mel_AudioIn_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    if (!g_ain.hotplug)
        g_ain.hotplug = mel_audioin_android__hotplug_start();
    ain_refresh_devices();

    for (usize i = 0; i < g_ain.devices.count; i++)
    {
        AIn_Device*     d = &g_ain.devices.items[i];
        Mel_AudioIn_Raw raw = {
            .stable_id = d->stable_id,
            .name = d->name,
            .kind = d->kind,
            .channels = d->channels,
            .samplerate = d->samplerate,
            .samplerates = d->rates.items,
            .samplerate_count = (u32)d->rates.count,
            .caps = { .gain = false },
        };
        if (!fn(&raw, fn_user))
            return;
    }
}

static str8 ain_default_id(void* user)
{
    MEL_UNUSED(user);
    for (usize i = 0; i < g_ain.devices.count; i++)
        if (g_ain.devices.items[i].kind == &mel_audioin_builtin)
            return g_ain.devices.items[i].stable_id;
    return STR8_EMPTY;
}

static Mel_AudioIn_Status ain_open(void* user, str8 stable_id, Mel_AudioIn_Sink sink, Mel_AudioIn_Open_Opt opt, Mel_AudioIn_Granted* granted)
{
    MEL_UNUSED(user);
    ain_reap();

    if (!mel_audioin_android__permission_granted())
    {
        mel_log_error("audioin", "android: open %.*s without RECORD_AUDIO permission", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_DENIED;
    }

    AIn_Stream* st = ain_stream_find(stable_id);
    if (st)
    {
        if (!ain_sink_add(st, sink))
            return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_LOST;
        if (!ain_opt_equal(opt, st->opt))
            mel_log_warn("audioin", "android: open options differ from the live stream on %.*s; first open's configuration applies", (int)stable_id.len, stable_id.data);
        if (granted)
            *granted = st->granted;
        return MEL_AUDIOIN_OK;
    }

    AIn_Device* dev = ain_device_find(stable_id);
    if (!dev)
    {
        mel_log_error("audioin", "android: open unknown device %.*s", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_NO_DEVICE;
    }

    Mel_AudioIn_Status fail_status = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    st = ain_stream_open(dev, opt, &fail_status);
    if (!st)
        return fail_status;
    if (!ain_sink_add(st, sink))
    {
        AAudioStream_close(st->stream);
        ain_stream_destroy(st);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    aaudio_result_t res = AAudioStream_requestStart(st->stream);
    if (res != AAUDIO_OK)
    {
        mel_log_error("audioin", "android: input requestStart failed for %.*s: %s", (int)stable_id.len, stable_id.data, AAudio_convertResultToText(res));
        AAudioStream_close(st->stream);
        ain_stream_destroy(st);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    mel_array_push(&g_ain.streams, st);
    if (granted)
        *granted = st->granted;
    mel_log_info("audioin",
                 "android: input stream opened on %.*s (%u ch @ %u Hz, %s%s)",
                 (int)stable_id.len,
                 stable_id.data,
                 st->channels,
                 st->samplerate,
                 st->granted.exclusive ? "exclusive" : "shared",
                 st->granted.processing.echo_cancellation ? ", voice-processed" : "");
    return MEL_AUDIOIN_OK;
}

static void ain_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    ain_reap();

    AIn_Stream* st = ain_stream_find(stable_id);
    if (!st)
        return;

    pthread_mutex_lock(&st->lock);
    if (st->lost_handled || st->closing)
    {
        pthread_mutex_unlock(&st->lock);
        return;
    }
    Sink_List* cur = atomic_load_explicit(&st->sinks, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    if (count == 0)
    {
        pthread_mutex_unlock(&st->lock);
        return;
    }
    Sink_List* nl = mel_alloc(g_ain.alloc, sizeof(Sink_List) + sizeof(Mel_AudioIn_Sink) * (usize)count);
    if (!nl)
    {
        pthread_mutex_unlock(&st->lock);
        return;
    }
    u32 kept = 0;
    for (u32 i = 0; i < count; i++)
        if (cur->sinks[i].token != token)
            nl->sinks[kept++] = cur->sinks[i];
    nl->count = kept;
    atomic_store_explicit(&st->sinks, nl, memory_order_release);
    mel_array_push(&st->garbage, cur);
    bool last = kept == 0;
    if (last)
        st->closing = true;
    pthread_mutex_unlock(&st->lock);

    if (!last)
        return;

    aaudio_result_t res = AAudioStream_requestStop(st->stream);
    if (res != AAUDIO_OK)
        mel_log_error("audioin", "android: input requestStop failed for %.*s: %s", (int)stable_id.len, stable_id.data, AAudio_convertResultToText(res));
    res = AAudioStream_close(st->stream);
    if (res != AAUDIO_OK)
        mel_log_error("audioin", "android: input close failed for %.*s: %s", (int)stable_id.len, stable_id.data, AAudio_convertResultToText(res));

    for (usize i = 0; i < g_ain.streams.count; i++)
    {
        if (g_ain.streams.items[i] == st)
        {
            mel_array_remove_unordered(&g_ain.streams, i);
            break;
        }
    }
    ain_stream_destroy(st);
}

static const mel_audioin_auth* ain_authorization(void* user)
{
    MEL_UNUSED(user);
    if (mel_audioin_android__permission_granted())
        return &mel_audioin_auth_granted;
    pthread_mutex_lock(&g_auth_lock);
    bool denied = g_ain.answered_denied;
    pthread_mutex_unlock(&g_auth_lock);
    return denied ? &mel_audioin_auth_denied : &mel_audioin_auth_not_determined;
}

static void ain_on_permission_result(void* user, i32 request_code, bool granted)
{
    MEL_UNUSED(user);
    MEL_UNUSED(request_code);

    AIn_Sink_Array pending;
    pthread_mutex_lock(&g_auth_lock);
    g_ain.answered_denied = !granted;
    pending = g_ain.auth_sinks;
    mel_array_init(&g_ain.auth_sinks, g_ain.alloc);
    pthread_mutex_unlock(&g_auth_lock);

    const mel_audioin_auth* auth = granted ? &mel_audioin_auth_granted : &mel_audioin_auth_denied;
    for (usize i = 0; i < pending.count; i++)
        if (pending.items[i].on_auth)
            pending.items[i].on_auth(pending.items[i].token, auth);
    mel_array_free(&pending);
}

static void ain_authorize(void* user, Mel_AudioIn_Sink sink)
{
    MEL_UNUSED(user);
    if (!sink.on_auth)
        return;
    if (mel_audioin_android__permission_granted())
    {
        sink.on_auth(sink.token, &mel_audioin_auth_granted);
        return;
    }

    pthread_mutex_lock(&g_auth_lock);
    if (!g_ain.permission_listening)
    {
        mel_platform_android_permission_listen(g_ain.alloc, MEL_AUDIOIN_ANDROID_PERMISSION_REQUEST_CODE, ain_on_permission_result, NULL);
        g_ain.permission_listening = true;
    }
    mel_array_push(&g_ain.auth_sinks, sink);
    pthread_mutex_unlock(&g_auth_lock);

    if (!mel_audioin_android__request_permission())
    {
        bool still_pending = false;
        pthread_mutex_lock(&g_auth_lock);
        for (usize i = g_ain.auth_sinks.count; i > 0; i--)
        {
            if (g_ain.auth_sinks.items[i - 1].token == sink.token && g_ain.auth_sinks.items[i - 1].on_auth == sink.on_auth)
            {
                mel_array_remove_ordered(&g_ain.auth_sinks, i - 1);
                still_pending = true;
                break;
            }
        }
        pthread_mutex_unlock(&g_auth_lock);
        mel_log_error("audioin", "android: RECORD_AUDIO request could not be issued");
        if (still_pending)
            sink.on_auth(sink.token, &mel_audioin_auth_denied);
    }
}

static void* ain_native(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    AIn_Stream* st = ain_stream_find(stable_id);
    return st ? (void*)st->stream : NULL;
}

static void ain_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);

    if (g_ain.hotplug)
    {
        mel_audioin_android__hotplug_stop();
        g_ain.hotplug = false;
    }
    if (g_ain.permission_listening)
    {
        mel_platform_android_permission_unlisten(MEL_AUDIOIN_ANDROID_PERMISSION_REQUEST_CODE, ain_on_permission_result, NULL);
        g_ain.permission_listening = false;
    }

    ain_reap();
    for (usize i = 0; i < g_ain.streams.count; i++)
    {
        AIn_Stream* st = g_ain.streams.items[i];

        pthread_mutex_lock(&st->lock);
        st->closing = true;
        Sink_List* sl = atomic_exchange_explicit(&st->sinks, NULL, memory_order_acq_rel);
        if (sl)
            mel_array_push(&st->garbage, sl);
        pthread_mutex_unlock(&st->lock);

        if (sl)
        {
            mel_log_warn("audioin", "android: input stream on %.*s still live at shutdown; releasing", (int)st->stable_id.len, st->stable_id.data);
            for (u32 j = 0; j < sl->count; j++)
                if (sl->sinks[j].on_lost)
                    sl->sinks[j].on_lost(sl->sinks[j].token);
        }

        AAudioStream_requestStop(st->stream);
        aaudio_result_t res = AAudioStream_close(st->stream);
        if (res != AAUDIO_OK)
            mel_log_error("audioin", "android: input close failed at shutdown: %s", AAudio_convertResultToText(res));
        ain_stream_destroy(st);
    }
    mel_array_free(&g_ain.streams);

    ain_devices_clear();
    mel_array_free(&g_ain.devices);

    pthread_mutex_lock(&g_auth_lock);
    mel_array_free(&g_ain.auth_sinks);
    pthread_mutex_unlock(&g_auth_lock);

    memset(&g_ain, 0, sizeof g_ain);
}

void mel_audioin_android__on_devices_changed(void)
{
    if (g_ain.registered)
        mel_audioin_provider_notify(g_ain.provider);
}

void mel_audioin__register_host_providers(void)
{
    static const Mel_AudioIn_Provider_Desc desc = {
        .name = "android-audiomanager",
        .enumerate = ain_enumerate,
        .default_id = ain_default_id,
        .open = ain_open,
        .close = ain_close,
        .authorization = ain_authorization,
        .authorize = ain_authorize,
        .native = ain_native,
        .shutdown = ain_shutdown,
    };
    g_ain.alloc = mel_alloc_heap();
    mel_array_init(&g_ain.devices, g_ain.alloc);
    mel_array_init(&g_ain.streams, g_ain.alloc);
    mel_array_init(&g_ain.auth_sinks, g_ain.alloc);
    g_ain.provider = mel_audioin_provider_register(&desc);
    g_ain.registered = true;
}
