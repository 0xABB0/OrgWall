#include <audioin/provider.h>

#include <core/types.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <thread/thread.h>
#include <log/log.h>

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID
#include <initguid.h>
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioCaptureClient, 0xC8ADBD64, 0xE71E, 0x48A0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);
DEFINE_GUID(IID_IMMNotificationClient, 0x7991EEC9, 0x7E89, 0x4D85, 0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0);
DEFINE_GUID(IID_IAudioEndpointVolume, 0x5CDF2C82, 0x841E, 0x4546, 0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A);

#include <stdatomic.h>
#include <string.h>

#define MEL_AUDIOIN_WASAPI_LOOPBACK_POLL_MS 100u

typedef struct
{
    u32              count;
    Mel_AudioIn_Sink sinks[];
} Sink_List;

typedef struct
{
    str8                 stable_id;
    bool                 loopback;
    bool                 exclusive;
    bool                 fmt_float;
    IMMDevice*           device;
    IAudioClient*        client;
    IAudioCaptureClient* capture;
    HANDLE               audio_event;
    HANDLE               stop_event;
    u32                  channels;
    u32                  samplerate;
    u32                  buffer_frames;
    f32*                 scratch;
    Mel_Thread           worker;
    bool                 worker_spawned;
    _Atomic(u32)         lost;
    _Atomic(void*)       sinks;
    Mel_Array(void*) garbage;
} Engine;

typedef struct
{
    str8                    stable_id;
    str8                    name;
    IMMDevice*              device;
    const mel_audioin_kind* kind;
    u32                     channels;
    u32                     samplerate;
    bool                    gain;
    bool                    loopback;
} Device_Rec;

typedef struct
{
    bool                 registered;
    const Mel_Alloc*     alloc;
    IMMDeviceEnumerator* enumerator;
    bool                 com_owned;
    bool                 notify_registered;
    Mel_Array(Device_Rec) devices;
    Mel_Array(Engine*) engines;
    str8                 default_id;
    Mel_AudioIn_Provider provider;
} Wasapi_In;

static Wasapi_In g_in;

typedef struct
{
    IMMNotificationClientVtbl* lpVtbl;
    _Atomic(LONG)              refs;
} Wasapi_In_Notify;

static Wasapi_In_Notify g_in_notify;

static void wasapi_log_hr(const char* what, HRESULT hr) { mel_log_error("audioin", "%s failed (hr=0x%08lx)", what, (unsigned long)hr); }

static void wasapi_str8_free(str8* s)
{
    if (s->data != NULL)
        mel_dealloc(g_in.alloc, s->data);
    *s = STR8_EMPTY;
}

static str8 wasapi_utf8_from_wide(const WCHAR* w)
{
    if (w == NULL)
        return STR8_EMPTY;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0)
        return STR8_EMPTY;
    u8* buf = (u8*)mel_alloc(g_in.alloc, (usize)bytes);
    if (buf == NULL)
        return STR8_EMPTY;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, (LPSTR)buf, bytes, NULL, NULL);
    return (str8){ buf, (size)(bytes - 1) };
}

static HRESULT STDMETHODCALLTYPE wasapi_notify_query(IMMNotificationClient* self, REFIID riid, void** out)
{
    if (out == NULL)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IMMNotificationClient))
    {
        *out = self;
        atomic_fetch_add_explicit(&g_in_notify.refs, 1, memory_order_relaxed);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE wasapi_notify_addref(IMMNotificationClient* self)
{
    MEL_UNUSED(self);
    return (ULONG)(atomic_fetch_add_explicit(&g_in_notify.refs, 1, memory_order_relaxed) + 1);
}

static ULONG STDMETHODCALLTYPE wasapi_notify_release(IMMNotificationClient* self)
{
    MEL_UNUSED(self);
    return (ULONG)(atomic_fetch_sub_explicit(&g_in_notify.refs, 1, memory_order_relaxed) - 1);
}

static HRESULT STDMETHODCALLTYPE wasapi_notify_state_changed(IMMNotificationClient* self, LPCWSTR id, DWORD state)
{
    MEL_UNUSED(self);
    MEL_UNUSED(id);
    MEL_UNUSED(state);
    mel_audioin_provider_notify(g_in.provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wasapi_notify_device_added(IMMNotificationClient* self, LPCWSTR id)
{
    MEL_UNUSED(self);
    MEL_UNUSED(id);
    mel_audioin_provider_notify(g_in.provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wasapi_notify_device_removed(IMMNotificationClient* self, LPCWSTR id)
{
    MEL_UNUSED(self);
    MEL_UNUSED(id);
    mel_audioin_provider_notify(g_in.provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wasapi_notify_default_changed(IMMNotificationClient* self, EDataFlow flow, ERole role, LPCWSTR id)
{
    MEL_UNUSED(self);
    MEL_UNUSED(id);
    if (flow == eCapture && role == eConsole)
        mel_audioin_provider_notify(g_in.provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wasapi_notify_property_changed(IMMNotificationClient* self, LPCWSTR id, const PROPERTYKEY key)
{
    MEL_UNUSED(self);
    MEL_UNUSED(id);
    MEL_UNUSED(key);
    return S_OK;
}

static IMMNotificationClientVtbl g_in_notify_vtbl = {
    .QueryInterface = wasapi_notify_query,
    .AddRef = wasapi_notify_addref,
    .Release = wasapi_notify_release,
    .OnDeviceStateChanged = wasapi_notify_state_changed,
    .OnDeviceAdded = wasapi_notify_device_added,
    .OnDeviceRemoved = wasapi_notify_device_removed,
    .OnDefaultDeviceChanged = wasapi_notify_default_changed,
    .OnPropertyValueChanged = wasapi_notify_property_changed,
};

static bool wasapi_format_is_float32(const WAVEFORMATEX* f)
{
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return f->wBitsPerSample == 32u;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE* x = (const WAVEFORMATEXTENSIBLE*)f;
        return IsEqualGUID(&x->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) && f->wBitsPerSample == 32u;
    }
    return false;
}

static bool wasapi_format_is_pcm16(const WAVEFORMATEX* f)
{
    if (f->wFormatTag == WAVE_FORMAT_PCM)
        return f->wBitsPerSample == 16u;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE* x = (const WAVEFORMATEXTENSIBLE*)f;
        return IsEqualGUID(&x->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM) && f->wBitsPerSample == 16u;
    }
    return false;
}

static void wasapi_build_pcm16_format(WAVEFORMATEXTENSIBLE* ext, u32 channels, u32 rate)
{
    memset(ext, 0, sizeof *ext);
    ext->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    ext->Format.nChannels = (WORD)channels;
    ext->Format.nSamplesPerSec = (DWORD)rate;
    ext->Format.wBitsPerSample = 16u;
    ext->Format.nBlockAlign = (WORD)(channels * 2u);
    ext->Format.nAvgBytesPerSec = (DWORD)(rate * channels * 2u);
    ext->Format.cbSize = (WORD)(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
    ext->Samples.wValidBitsPerSample = 16u;
    ext->dwChannelMask = channels == 1u ? KSAUDIO_SPEAKER_MONO : KSAUDIO_SPEAKER_STEREO;
    ext->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
}

static Device_Rec* wasapi_device_find(str8 stable_id)
{
    for (usize i = 0; i < g_in.devices.count; i++)
        if (str8_equals(g_in.devices.items[i].stable_id, stable_id))
            return &g_in.devices.items[i];
    return NULL;
}

static Engine* wasapi_engine_find(str8 stable_id)
{
    for (usize i = 0; i < g_in.engines.count; i++)
        if (str8_equals(g_in.engines.items[i]->stable_id, stable_id))
            return g_in.engines.items[i];
    return NULL;
}

static void wasapi_device_rec_free(Device_Rec* rec)
{
    wasapi_str8_free(&rec->stable_id);
    wasapi_str8_free(&rec->name);
    if (rec->device != NULL)
    {
        IMMDevice_Release(rec->device);
        rec->device = NULL;
    }
}

static void wasapi_devices_clear(void)
{
    for (usize i = 0; i < g_in.devices.count; i++)
        wasapi_device_rec_free(&g_in.devices.items[i]);
    mel_array_clear(&g_in.devices);
}

static bool wasapi_device_mix_format(IMMDevice* dev, u32* channels, u32* samplerate)
{
    IAudioClient* client = NULL;
    HRESULT       hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&client);
    if (FAILED(hr) || client == NULL)
        return false;
    WAVEFORMATEX* mix = NULL;
    hr = IAudioClient_GetMixFormat(client, &mix);
    bool ok = SUCCEEDED(hr) && mix != NULL && mix->nChannels > 0u && mix->nSamplesPerSec > 0u;
    if (ok)
    {
        *channels = (u32)mix->nChannels;
        *samplerate = (u32)mix->nSamplesPerSec;
    }
    if (mix != NULL)
        CoTaskMemFree(mix);
    IAudioClient_Release(client);
    return ok;
}

static const mel_audioin_kind* wasapi_device_kind(IPropertyStore* props)
{
    if (props == NULL)
        return &mel_audioin_unknown;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    const mel_audioin_kind* kind = &mel_audioin_unknown;
    if (SUCCEEDED(IPropertyStore_GetValue(props, &PKEY_AudioEndpoint_FormFactor, &pv)) && pv.vt == VT_UI4)
        if (pv.ulVal == Microphone || pv.ulVal == Headset)
            kind = &mel_audioin_builtin;
    PropVariantClear(&pv);
    return kind;
}

static str8 wasapi_device_friendly_name(IPropertyStore* props)
{
    if (props == NULL)
        return STR8_EMPTY;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    str8 name = STR8_EMPTY;
    if (SUCCEEDED(IPropertyStore_GetValue(props, &PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR)
        name = wasapi_utf8_from_wide(pv.pwszVal);
    PropVariantClear(&pv);
    return name;
}

static bool wasapi_device_gain_cap(IMMDevice* dev)
{
    IAudioEndpointVolume* vol = NULL;
    HRESULT               hr = IMMDevice_Activate(dev, &IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&vol);
    if (FAILED(hr) || vol == NULL)
        return false;
    IAudioEndpointVolume_Release(vol);
    return true;
}

static void wasapi_device_rec_build(IMMDevice* dev, bool loopback)
{
    WCHAR*  wid = NULL;
    HRESULT hr = IMMDevice_GetId(dev, &wid);
    if (FAILED(hr) || wid == NULL)
    {
        wasapi_log_hr("IMMDevice::GetId", hr);
        IMMDevice_Release(dev);
        return;
    }
    str8 idu = wasapi_utf8_from_wide(wid);
    CoTaskMemFree(wid);
    if (idu.data == NULL)
    {
        IMMDevice_Release(dev);
        return;
    }

    Device_Rec rec;
    memset(&rec, 0, sizeof rec);
    rec.device = dev;
    rec.loopback = loopback;
    if (loopback)
        rec.stable_id = str8_fmt(g_in.alloc, "wasapi-loopback:%.*s", (int)idu.len, idu.data);
    else
        rec.stable_id = str8_fmt(g_in.alloc, "wasapi:%.*s", (int)idu.len, idu.data);
    mel_dealloc(g_in.alloc, idu.data);

    if (!wasapi_device_mix_format(dev, &rec.channels, &rec.samplerate))
    {
        mel_log_warn("audioin", "skipping endpoint %.*s: mix format unavailable", (int)rec.stable_id.len, rec.stable_id.data);
        wasapi_device_rec_free(&rec);
        return;
    }

    IPropertyStore* props = NULL;
    if (FAILED(IMMDevice_OpenPropertyStore(dev, STGM_READ, &props)))
        props = NULL;
    rec.name = wasapi_device_friendly_name(props);
    rec.kind = loopback ? &mel_audioin_loopback : wasapi_device_kind(props);
    if (props != NULL)
        IPropertyStore_Release(props);

    if (rec.name.data == NULL)
    {
        mel_log_warn("audioin", "endpoint %.*s has no friendly name; using stable id", (int)rec.stable_id.len, rec.stable_id.data);
        rec.name = str8_dup(rec.stable_id, g_in.alloc);
    }
    if (loopback)
    {
        str8 decorated = str8_fmt(g_in.alloc, "%.*s (loopback)", (int)rec.name.len, rec.name.data);
        wasapi_str8_free(&rec.name);
        rec.name = decorated;
    }

    rec.gain = loopback ? false : wasapi_device_gain_cap(dev);

    mel_array_push(&g_in.devices, rec);
}

static void wasapi_devices_append_flow(EDataFlow flow, bool loopback)
{
    IMMDeviceCollection* coll = NULL;
    HRESULT              hr = IMMDeviceEnumerator_EnumAudioEndpoints(g_in.enumerator, flow, DEVICE_STATE_ACTIVE, &coll);
    if (FAILED(hr) || coll == NULL)
    {
        wasapi_log_hr("IMMDeviceEnumerator::EnumAudioEndpoints", hr);
        return;
    }
    UINT n = 0;
    hr = IMMDeviceCollection_GetCount(coll, &n);
    if (FAILED(hr))
    {
        wasapi_log_hr("IMMDeviceCollection::GetCount", hr);
        IMMDeviceCollection_Release(coll);
        return;
    }
    for (UINT i = 0; i < n; i++)
    {
        IMMDevice* dev = NULL;
        if (FAILED(IMMDeviceCollection_Item(coll, i, &dev)) || dev == NULL)
            continue;
        wasapi_device_rec_build(dev, loopback);
    }
    IMMDeviceCollection_Release(coll);
}

static void wasapi_enumerate(void* user, Mel_AudioIn_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    wasapi_devices_clear();
    if (g_in.enumerator == NULL)
    {
        mel_log_error("audioin", "wasapi enumerate without a device enumerator; COM setup failed at registration");
        return;
    }
    wasapi_devices_append_flow(eCapture, false);
    wasapi_devices_append_flow(eRender, true);

    for (usize i = 0; i < g_in.devices.count; i++)
    {
        Device_Rec*     rec = &g_in.devices.items[i];
        Mel_AudioIn_Raw raw = {
            .stable_id = rec->stable_id,
            .name = rec->name,
            .kind = rec->kind,
            .channels = rec->channels,
            .samplerate = rec->samplerate,
            .samplerates = &rec->samplerate,
            .samplerate_count = 1,
            .caps = { .gain = rec->gain },
        };
        if (!fn(&raw, fn_user))
            return;
    }
}

static str8 wasapi_default_id(void* user)
{
    MEL_UNUSED(user);
    if (g_in.enumerator == NULL)
        return STR8_EMPTY;
    IMMDevice* dev = NULL;
    HRESULT    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(g_in.enumerator, eCapture, eConsole, &dev);
    if (FAILED(hr) || dev == NULL)
    {
        mel_log_warn("audioin", "GetDefaultAudioEndpoint(eCapture) failed (hr=0x%08lx); no default input", (unsigned long)hr);
        return STR8_EMPTY;
    }
    WCHAR* wid = NULL;
    hr = IMMDevice_GetId(dev, &wid);
    IMMDevice_Release(dev);
    if (FAILED(hr) || wid == NULL)
    {
        wasapi_log_hr("IMMDevice::GetId(default)", hr);
        return STR8_EMPTY;
    }
    str8 idu = wasapi_utf8_from_wide(wid);
    CoTaskMemFree(wid);
    if (idu.data == NULL)
        return STR8_EMPTY;
    wasapi_str8_free(&g_in.default_id);
    g_in.default_id = str8_fmt(g_in.alloc, "wasapi:%.*s", (int)idu.len, idu.data);
    mel_dealloc(g_in.alloc, idu.data);
    return g_in.default_id;
}

static u64 wasapi_ts_advance(u64 timestamp_ns, u32 frames, u32 samplerate)
{
    if (timestamp_ns == 0ull || samplerate == 0u)
        return 0ull;
    return timestamp_ns + (u64)frames * 1000000000ull / samplerate;
}

static void wasapi_engine_deliver(Engine* e, const f32* samples, u32 frames, u64 timestamp_ns)
{
    Sink_List* sl = atomic_load_explicit(&e->sinks, memory_order_acquire);
    if (sl == NULL)
        return;
    for (u32 i = 0; i < sl->count; i++)
        if (sl->sinks[i].on_frames)
            sl->sinks[i].on_frames(sl->sinks[i].token, samples, frames, e->samplerate, e->channels, timestamp_ns);
}

static void wasapi_engine_fire_lost(Engine* e)
{
    if (atomic_exchange_explicit(&e->lost, 1u, memory_order_acq_rel) != 0u)
        return;
    Sink_List* sl = atomic_load_explicit(&e->sinks, memory_order_acquire);
    if (sl == NULL)
        return;
    for (u32 i = 0; i < sl->count; i++)
        if (sl->sinks[i].on_lost)
            sl->sinks[i].on_lost(sl->sinks[i].token);
}

static void wasapi_engine_deliver_silent(Engine* e, u32 frames, u64 timestamp_ns)
{
    u32 left = frames;
    u64 ts = timestamp_ns;
    while (left > 0u)
    {
        u32 n = left < e->buffer_frames ? left : e->buffer_frames;
        memset(e->scratch, 0, sizeof(f32) * (usize)n * e->channels);
        wasapi_engine_deliver(e, e->scratch, n, ts);
        ts = wasapi_ts_advance(ts, n, e->samplerate);
        left -= n;
    }
}

static void wasapi_engine_deliver_pcm16(Engine* e, const i16* src, u32 frames, u64 timestamp_ns)
{
    u32 left = frames;
    u64 ts = timestamp_ns;
    while (left > 0u)
    {
        u32 n = left < e->buffer_frames ? left : e->buffer_frames;
        u32 samples = n * e->channels;
        for (u32 i = 0; i < samples; i++)
            e->scratch[i] = (f32)src[i] * (1.0f / 32768.0f);
        wasapi_engine_deliver(e, e->scratch, n, ts);
        ts = wasapi_ts_advance(ts, n, e->samplerate);
        src += samples;
        left -= n;
    }
}

static bool wasapi_engine_drain(Engine* e, bool* lost)
{
    for (;;)
    {
        UINT32  packet = 0;
        HRESULT hr = IAudioCaptureClient_GetNextPacketSize(e->capture, &packet);
        if (FAILED(hr))
        {
            *lost = hr == AUDCLNT_E_DEVICE_INVALIDATED;
            wasapi_log_hr("IAudioCaptureClient::GetNextPacketSize", hr);
            return false;
        }
        if (packet == 0u)
            return true;

        BYTE*  data = NULL;
        UINT32 frames = 0;
        DWORD  flags = 0;
        UINT64 qpc = 0;
        hr = IAudioCaptureClient_GetBuffer(e->capture, &data, &frames, &flags, NULL, &qpc);
        if (hr == AUDCLNT_S_BUFFER_EMPTY)
            return true;
        if (FAILED(hr))
        {
            *lost = hr == AUDCLNT_E_DEVICE_INVALIDATED;
            wasapi_log_hr("IAudioCaptureClient::GetBuffer", hr);
            return false;
        }

        u64 timestamp_ns = ((flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0u || qpc == 0ull) ? 0ull : (u64)qpc * 100ull;

        if (frames > 0u)
        {
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                wasapi_engine_deliver_silent(e, (u32)frames, timestamp_ns);
            else if (e->fmt_float)
                wasapi_engine_deliver(e, (const f32*)data, (u32)frames, timestamp_ns);
            else
                wasapi_engine_deliver_pcm16(e, (const i16*)data, (u32)frames, timestamp_ns);
        }

        hr = IAudioCaptureClient_ReleaseBuffer(e->capture, frames);
        if (FAILED(hr))
        {
            *lost = hr == AUDCLNT_E_DEVICE_INVALIDATED;
            wasapi_log_hr("IAudioCaptureClient::ReleaseBuffer", hr);
            return false;
        }
    }
}

static int wasapi_engine_worker(void* user)
{
    Engine* e = user;

    HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool    co_owned = !FAILED(co) && co != RPC_E_CHANGED_MODE;

    HRESULT hr = IAudioClient_Start(e->client);
    if (FAILED(hr))
    {
        wasapi_log_hr("IAudioClient::Start", hr);
        wasapi_engine_fire_lost(e);
        if (co_owned)
            CoUninitialize();
        return 1;
    }

    HANDLE waits[2] = { e->stop_event, e->audio_event };
    DWORD  timeout = e->loopback ? MEL_AUDIOIN_WASAPI_LOOPBACK_POLL_MS : INFINITE;
    bool   lost = false;
    for (;;)
    {
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, timeout);
        if (w == WAIT_OBJECT_0)
            break;
        if (w != WAIT_OBJECT_0 + 1 && w != WAIT_TIMEOUT)
        {
            mel_log_error("audioin", "capture wait returned %lu (err=%lu)", (unsigned long)w, (unsigned long)GetLastError());
            break;
        }
        if (!wasapi_engine_drain(e, &lost))
            break;
    }

    IAudioClient_Stop(e->client);
    IAudioClient_Reset(e->client);
    if (lost)
        wasapi_engine_fire_lost(e);
    if (co_owned)
        CoUninitialize();
    return lost ? 1 : 0;
}

static void wasapi_engine_free(Engine* e)
{
    if (e->worker_spawned)
    {
        SetEvent(e->stop_event);
        mel_thread_join(&e->worker, NULL);
        e->worker_spawned = false;
    }
    Sink_List* sl = atomic_exchange_explicit(&e->sinks, NULL, memory_order_acq_rel);
    if (sl != NULL)
        mel_dealloc(g_in.alloc, sl);
    for (usize i = 0; i < e->garbage.count; i++)
        mel_dealloc(g_in.alloc, e->garbage.items[i]);
    mel_array_free(&e->garbage);
    if (e->capture != NULL)
        IAudioCaptureClient_Release(e->capture);
    if (e->client != NULL)
        IAudioClient_Release(e->client);
    if (e->device != NULL)
        IMMDevice_Release(e->device);
    if (e->audio_event != NULL)
        CloseHandle(e->audio_event);
    if (e->stop_event != NULL)
        CloseHandle(e->stop_event);
    if (e->scratch != NULL)
        mel_dealloc(g_in.alloc, e->scratch);
    wasapi_str8_free(&e->stable_id);
    mel_dealloc(g_in.alloc, e);
}

static void wasapi_engine_sinks_swap(Engine* e, Sink_List* nl)
{
    void* old = atomic_exchange_explicit(&e->sinks, nl, memory_order_acq_rel);
    if (old != NULL)
        mel_array_push(&e->garbage, old);
}

static bool wasapi_engine_sink_add(Engine* e, Mel_AudioIn_Sink sink)
{
    Sink_List* cur = atomic_load_explicit(&e->sinks, memory_order_acquire);
    u32        count = cur != NULL ? cur->count : 0u;
    Sink_List* nl = mel_alloc(g_in.alloc, sizeof(Sink_List) + sizeof(Mel_AudioIn_Sink) * ((usize)count + 1u));
    if (nl == NULL)
        return false;
    for (u32 i = 0; i < count; i++)
        nl->sinks[i] = cur->sinks[i];
    nl->sinks[count] = sink;
    nl->count = count + 1u;
    wasapi_engine_sinks_swap(e, nl);
    return true;
}

static bool wasapi_engine_activate(Engine* e)
{
    if (e->client != NULL)
    {
        IAudioClient_Release(e->client);
        e->client = NULL;
    }
    HRESULT hr = IMMDevice_Activate(e->device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&e->client);
    if (FAILED(hr) || e->client == NULL)
    {
        wasapi_log_hr("IMMDevice::Activate(IAudioClient)", hr);
        return false;
    }
    return true;
}

static Mel_AudioIn_Status wasapi_engine_try_exclusive(Engine* e, const WAVEFORMATEX* mix, REFERENCE_TIME default_period, bool* granted_exclusive)
{
    *granted_exclusive = false;

    WAVEFORMATEXTENSIBLE fmt;
    wasapi_build_pcm16_format(&fmt, (u32)mix->nChannels, (u32)mix->nSamplesPerSec);

    HRESULT hr = IAudioClient_IsFormatSupported(e->client, AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)&fmt, NULL);
    if (hr == AUDCLNT_E_DEVICE_IN_USE)
    {
        mel_log_error("audioin", "exclusive open: device in use: %.*s", (int)e->stable_id.len, e->stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_BUSY;
    }
    if (hr != S_OK)
    {
        mel_log_warn("audioin",
                     "exclusive format (pcm16 %uch @ %uHz) rejected (hr=0x%08lx) on %.*s; falling back to shared",
                     (u32)fmt.Format.nChannels,
                     (u32)fmt.Format.nSamplesPerSec,
                     (unsigned long)hr,
                     (int)e->stable_id.len,
                     e->stable_id.data);
        return MEL_AUDIOIN_OK;
    }

    REFERENCE_TIME period = default_period;
    hr = IAudioClient_Initialize(e->client, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, (WAVEFORMATEX*)&fmt, NULL);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
    {
        UINT32  aligned = 0;
        HRESULT bh = IAudioClient_GetBufferSize(e->client, &aligned);
        if (SUCCEEDED(bh) && aligned > 0u && fmt.Format.nSamplesPerSec > 0u)
        {
            period = (REFERENCE_TIME)((10000000.0 * (f64)aligned / (f64)fmt.Format.nSamplesPerSec) + 0.5);
            if (!wasapi_engine_activate(e))
                return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
            hr = IAudioClient_Initialize(e->client, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, (WAVEFORMATEX*)&fmt, NULL);
        }
    }
    if (hr == AUDCLNT_E_DEVICE_IN_USE)
    {
        mel_log_error("audioin", "exclusive open: device in use: %.*s", (int)e->stable_id.len, e->stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_BUSY;
    }
    if (FAILED(hr))
    {
        mel_log_warn("audioin", "IAudioClient::Initialize(EXCLUSIVE,EVENTCALLBACK) failed (hr=0x%08lx) on %.*s; falling back to shared", (unsigned long)hr, (int)e->stable_id.len, e->stable_id.data);
        if (!wasapi_engine_activate(e))
            return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
        return MEL_AUDIOIN_OK;
    }

    e->fmt_float = false;
    e->channels = (u32)fmt.Format.nChannels;
    e->samplerate = (u32)fmt.Format.nSamplesPerSec;
    *granted_exclusive = true;
    return MEL_AUDIOIN_OK;
}

static Engine* wasapi_engine_create(Device_Rec* rec, Mel_AudioIn_Sink sink, Mel_AudioIn_Open_Opt opt, Mel_AudioIn_Status* status)
{
    *status = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    Engine* e = mel_alloc_type(g_in.alloc, Engine);
    if (e == NULL)
        return NULL;
    memset(e, 0, sizeof *e);
    mel_array_init(&e->garbage, g_in.alloc);
    e->stable_id = str8_dup(rec->stable_id, g_in.alloc);
    e->loopback = rec->loopback;
    e->device = rec->device;
    IMMDevice_AddRef(e->device);

    if (!wasapi_engine_activate(e))
    {
        wasapi_engine_free(e);
        return NULL;
    }

    WAVEFORMATEX* mix = NULL;
    HRESULT       hr = IAudioClient_GetMixFormat(e->client, &mix);
    if (FAILED(hr) || mix == NULL)
    {
        wasapi_log_hr("IAudioClient::GetMixFormat", hr);
        wasapi_engine_free(e);
        return NULL;
    }

    REFERENCE_TIME default_period = 0;
    REFERENCE_TIME min_period = 0;
    hr = IAudioClient_GetDevicePeriod(e->client, &default_period, &min_period);
    if (FAILED(hr))
    {
        wasapi_log_hr("IAudioClient::GetDevicePeriod", hr);
        CoTaskMemFree(mix);
        wasapi_engine_free(e);
        return NULL;
    }

    bool exclusive = false;
    if (opt.exclusive)
    {
        if (e->loopback)
        {
            mel_log_warn("audioin", "exclusive capture unavailable on loopback endpoint %.*s; lowering to shared", (int)e->stable_id.len, e->stable_id.data);
        }
        else
        {
            Mel_AudioIn_Status xs = wasapi_engine_try_exclusive(e, mix, default_period, &exclusive);
            if (mel_audioin_status_failed(xs))
            {
                CoTaskMemFree(mix);
                wasapi_engine_free(e);
                *status = xs;
                return NULL;
            }
        }
    }
    e->exclusive = exclusive;

    if (!exclusive)
    {
        if (wasapi_format_is_float32(mix))
            e->fmt_float = true;
        else if (wasapi_format_is_pcm16(mix))
            e->fmt_float = false;
        else
        {
            mel_log_error("audioin", "mix format unsupported for capture (tag=%u bits=%u) on %.*s", (u32)mix->wFormatTag, (u32)mix->wBitsPerSample, (int)e->stable_id.len, e->stable_id.data);
            CoTaskMemFree(mix);
            wasapi_engine_free(e);
            return NULL;
        }
        e->channels = (u32)mix->nChannels;
        e->samplerate = (u32)mix->nSamplesPerSec;

        DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        if (e->loopback)
            stream_flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
        hr = IAudioClient_Initialize(e->client, AUDCLNT_SHAREMODE_SHARED, stream_flags, default_period, 0, mix, NULL);
        if (FAILED(hr))
        {
            wasapi_log_hr("IAudioClient::Initialize(SHARED,EVENTCALLBACK)", hr);
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
                *status = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_LOST;
            CoTaskMemFree(mix);
            wasapi_engine_free(e);
            return NULL;
        }
    }
    CoTaskMemFree(mix);

    e->audio_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (e->audio_event == NULL)
    {
        mel_log_error("audioin", "CreateEventW(audio) failed (err=%lu)", (unsigned long)GetLastError());
        wasapi_engine_free(e);
        return NULL;
    }
    hr = IAudioClient_SetEventHandle(e->client, e->audio_event);
    if (FAILED(hr))
    {
        wasapi_log_hr("IAudioClient::SetEventHandle", hr);
        wasapi_engine_free(e);
        return NULL;
    }

    UINT32 buffer_frames = 0;
    hr = IAudioClient_GetBufferSize(e->client, &buffer_frames);
    if (FAILED(hr) || buffer_frames == 0u)
    {
        wasapi_log_hr("IAudioClient::GetBufferSize", hr);
        wasapi_engine_free(e);
        return NULL;
    }
    e->buffer_frames = (u32)buffer_frames;

    hr = IAudioClient_GetService(e->client, &IID_IAudioCaptureClient, (void**)&e->capture);
    if (FAILED(hr) || e->capture == NULL)
    {
        wasapi_log_hr("IAudioClient::GetService(IAudioCaptureClient)", hr);
        wasapi_engine_free(e);
        return NULL;
    }

    e->scratch = mel_alloc(g_in.alloc, sizeof(f32) * (usize)e->buffer_frames * e->channels);
    if (e->scratch == NULL)
    {
        wasapi_engine_free(e);
        return NULL;
    }

    e->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (e->stop_event == NULL)
    {
        mel_log_error("audioin", "CreateEventW(stop) failed (err=%lu)", (unsigned long)GetLastError());
        wasapi_engine_free(e);
        return NULL;
    }

    if (!wasapi_engine_sink_add(e, sink))
    {
        wasapi_engine_free(e);
        return NULL;
    }

    if (!mel_thread_spawn(&e->worker, wasapi_engine_worker, e, .name = "mel-audioin"))
    {
        mel_log_error("audioin", "failed to spawn capture worker for %.*s", (int)e->stable_id.len, e->stable_id.data);
        wasapi_engine_free(e);
        return NULL;
    }
    e->worker_spawned = true;

    mel_log_info("audioin",
                 "capture engine started: %.*s %uch @ %uHz (%s, %s, buffer %u frames)",
                 (int)e->stable_id.len,
                 e->stable_id.data,
                 e->channels,
                 e->samplerate,
                 e->exclusive ? "exclusive" : "shared",
                 e->fmt_float ? "f32" : "s16",
                 e->buffer_frames);
    *status = MEL_AUDIOIN_OK;
    return e;
}

static Mel_AudioIn_Status wasapi_open(void* user, str8 stable_id, Mel_AudioIn_Sink sink, Mel_AudioIn_Open_Opt opt, Mel_AudioIn_Granted* granted)
{
    MEL_UNUSED(user);
    assert(granted != NULL);
    memset(granted, 0, sizeof *granted);

    if (opt.processing.echo_cancellation || opt.processing.noise_suppression || opt.processing.auto_gain)
        mel_log_warn("audioin",
                     "voice processing (aec=%d ns=%d agc=%d) is APO/driver-owned on win32, not per-stream requestable; lowering to off for %.*s",
                     (int)opt.processing.echo_cancellation,
                     (int)opt.processing.noise_suppression,
                     (int)opt.processing.auto_gain,
                     (int)stable_id.len,
                     stable_id.data);

    Engine* e = wasapi_engine_find(stable_id);
    if (e != NULL)
    {
        if (atomic_load_explicit(&e->lost, memory_order_acquire) != 0u)
        {
            mel_log_error("audioin", "open on lost capture engine %.*s", (int)stable_id.len, stable_id.data);
            return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_LOST;
        }
        if (opt.exclusive != e->exclusive)
            mel_log_warn("audioin", "open requested exclusive=%d but engine on %.*s runs exclusive=%d; granting engine config", (int)opt.exclusive, (int)stable_id.len, stable_id.data, (int)e->exclusive);
        if (!wasapi_engine_sink_add(e, sink))
            return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
        granted->exclusive = e->exclusive;
        granted->os_timestamps = true;
        return MEL_AUDIOIN_OK;
    }

    Device_Rec* rec = wasapi_device_find(stable_id);
    if (rec == NULL)
    {
        mel_log_error("audioin", "open on unknown device %.*s", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_NO_DEVICE;
    }

    Mel_AudioIn_Status status;
    e = wasapi_engine_create(rec, sink, opt, &status);
    if (e == NULL)
        return status;
    mel_array_push(&g_in.engines, e);
    granted->exclusive = e->exclusive;
    granted->os_timestamps = true;
    return status;
}

static void wasapi_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    Engine* e = wasapi_engine_find(stable_id);
    if (e == NULL)
    {
        mel_log_error("audioin", "close on device without capture engine: %.*s", (int)stable_id.len, stable_id.data);
        return;
    }

    Sink_List* cur = atomic_load_explicit(&e->sinks, memory_order_acquire);
    u32        count = cur != NULL ? cur->count : 0u;
    if (count == 0u)
    {
        mel_log_error("audioin", "close with no open sinks on %.*s", (int)stable_id.len, stable_id.data);
        return;
    }

    Sink_List* nl = mel_alloc(g_in.alloc, sizeof(Sink_List) + sizeof(Mel_AudioIn_Sink) * (usize)count);
    if (nl == NULL)
        return;
    u32 kept = 0;
    for (u32 i = 0; i < count; i++)
        if (cur->sinks[i].token != token)
            nl->sinks[kept++] = cur->sinks[i];
    nl->count = kept;
    if (kept == count)
    {
        mel_log_error("audioin", "close with unknown token on %.*s", (int)stable_id.len, stable_id.data);
        mel_dealloc(g_in.alloc, nl);
        return;
    }
    wasapi_engine_sinks_swap(e, nl);

    if (kept > 0u)
        return;

    for (usize i = 0; i < g_in.engines.count; i++)
    {
        if (g_in.engines.items[i] == e)
        {
            g_in.engines.items[i] = g_in.engines.items[g_in.engines.count - 1];
            g_in.engines.count--;
            break;
        }
    }
    mel_log_info("audioin", "capture engine stopped: %.*s", (int)stable_id.len, stable_id.data);
    wasapi_engine_free(e);
}

static f32 wasapi_gain(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    Device_Rec* rec = wasapi_device_find(stable_id);
    if (rec == NULL || !rec->gain)
    {
        mel_log_error("audioin", "gain on device without gain capability: %.*s", (int)stable_id.len, stable_id.data);
        return 0.0f;
    }
    IAudioEndpointVolume* vol = NULL;
    HRESULT               hr = IMMDevice_Activate(rec->device, &IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&vol);
    if (FAILED(hr) || vol == NULL)
    {
        wasapi_log_hr("IMMDevice::Activate(IAudioEndpointVolume)", hr);
        return 0.0f;
    }
    float v = 0.0f;
    hr = IAudioEndpointVolume_GetMasterVolumeLevelScalar(vol, &v);
    IAudioEndpointVolume_Release(vol);
    if (FAILED(hr))
    {
        wasapi_log_hr("IAudioEndpointVolume::GetMasterVolumeLevelScalar", hr);
        return 0.0f;
    }
    return (f32)v;
}

static Mel_AudioIn_Status wasapi_set_gain(void* user, str8 stable_id, f32 gain)
{
    MEL_UNUSED(user);
    Device_Rec* rec = wasapi_device_find(stable_id);
    if (rec == NULL)
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_NO_DEVICE;
    if (!rec->gain)
    {
        mel_log_error("audioin", "set_gain on device without gain capability: %.*s", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    IAudioEndpointVolume* vol = NULL;
    HRESULT               hr = IMMDevice_Activate(rec->device, &IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&vol);
    if (FAILED(hr) || vol == NULL)
    {
        wasapi_log_hr("IMMDevice::Activate(IAudioEndpointVolume)", hr);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    hr = IAudioEndpointVolume_SetMasterVolumeLevelScalar(vol, (float)gain, NULL);
    IAudioEndpointVolume_Release(vol);
    if (FAILED(hr))
    {
        wasapi_log_hr("IAudioEndpointVolume::SetMasterVolumeLevelScalar", hr);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    return MEL_AUDIOIN_OK;
}

static const mel_audioin_auth* wasapi_authorization(void* user)
{
    MEL_UNUSED(user);
    return &mel_audioin_auth_granted;
}

static void wasapi_authorize(void* user, Mel_AudioIn_Sink sink)
{
    MEL_UNUSED(user);
    if (sink.on_auth)
        sink.on_auth(sink.token, &mel_audioin_auth_granted);
}

static void* wasapi_native(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    Device_Rec* rec = wasapi_device_find(stable_id);
    return rec != NULL ? (void*)rec->device : NULL;
}

static void wasapi_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    for (usize i = 0; i < g_in.engines.count; i++)
    {
        mel_log_warn("audioin", "capture engine %.*s still live at shutdown; releasing", (int)g_in.engines.items[i]->stable_id.len, g_in.engines.items[i]->stable_id.data);
        wasapi_engine_free(g_in.engines.items[i]);
    }
    mel_array_free(&g_in.engines);

    if (g_in.notify_registered && g_in.enumerator != NULL)
    {
        HRESULT hr = IMMDeviceEnumerator_UnregisterEndpointNotificationCallback(g_in.enumerator, (IMMNotificationClient*)&g_in_notify);
        if (FAILED(hr))
            wasapi_log_hr("IMMDeviceEnumerator::UnregisterEndpointNotificationCallback", hr);
        g_in.notify_registered = false;
    }

    wasapi_devices_clear();
    mel_array_free(&g_in.devices);
    wasapi_str8_free(&g_in.default_id);

    if (g_in.enumerator != NULL)
    {
        IMMDeviceEnumerator_Release(g_in.enumerator);
        g_in.enumerator = NULL;
    }
    if (g_in.com_owned)
        CoUninitialize();
    memset(&g_in, 0, sizeof g_in);
}

static const Mel_AudioIn_Provider_Desc WASAPI_DESC = {
    .name = "win32-wasapi",
    .enumerate = wasapi_enumerate,
    .default_id = wasapi_default_id,
    .open = wasapi_open,
    .close = wasapi_close,
    .gain = wasapi_gain,
    .set_gain = wasapi_set_gain,
    .authorization = wasapi_authorization,
    .authorize = wasapi_authorize,
    .native = wasapi_native,
    .shutdown = wasapi_shutdown,
};

void mel_audioin__register_host_providers(void)
{
    assert(!g_in.registered);
    g_in.alloc = mel_alloc_heap();
    mel_array_init(&g_in.devices, g_in.alloc);
    mel_array_init(&g_in.engines, g_in.alloc);
    g_in.default_id = STR8_EMPTY;
    g_in.provider = mel_audioin_provider_register(&WASAPI_DESC);
    g_in.registered = true;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE)
    {
        mel_log_warn("audioin", "COM already initialized in a different apartment; not owning teardown");
    }
    else if (FAILED(hr))
    {
        wasapi_log_hr("CoInitializeEx", hr);
        return;
    }
    else
    {
        g_in.com_owned = true;
    }

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&g_in.enumerator);
    if (FAILED(hr))
    {
        wasapi_log_hr("CoCreateInstance(MMDeviceEnumerator)", hr);
        g_in.enumerator = NULL;
        return;
    }

    g_in_notify.lpVtbl = &g_in_notify_vtbl;
    atomic_store_explicit(&g_in_notify.refs, 1, memory_order_relaxed);
    hr = IMMDeviceEnumerator_RegisterEndpointNotificationCallback(g_in.enumerator, (IMMNotificationClient*)&g_in_notify);
    if (FAILED(hr))
    {
        wasapi_log_hr("IMMDeviceEnumerator::RegisterEndpointNotificationCallback", hr);
        return;
    }
    g_in.notify_registered = true;
}
