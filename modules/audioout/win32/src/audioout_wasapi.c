#include <audioout/provider.h>

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
DEFINE_GUID(IID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);
DEFINE_GUID(IID_IMMNotificationClient, 0x7991EEC9, 0x7E89, 0x4D85, 0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0);
DEFINE_GUID(IID_IAudioEndpointVolume, 0x5CDF2C82, 0x841E, 0x4546, 0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A);
DEFINE_GUID(IID_IAudioEndpointVolumeCallback, 0x657804FA, 0xD6AD, 0x4496, 0x8A, 0x60, 0x35, 0x27, 0x52, 0xAF, 0x4F, 0x89);

#include <stdatomic.h>
#include <string.h>

#define MEL_AUDIOOUT_WASAPI_REFTIMES_PER_SEC 10000000LL

typedef struct
{
    Mel_AudioOut_Source src;
    bool                started;
} Open_Rec;

typedef struct
{
    u32      count;
    Open_Rec opens[];
} Open_List;

typedef struct
{
    str8                stable_id;
    bool                fmt_float;
    IMMDevice*          device;
    IAudioClient*       client;
    IAudioRenderClient* render;
    HANDLE              audio_event;
    HANDLE              wake_event;
    HANDLE              stop_event;
    u32                 channels;
    u32                 samplerate;
    u32                 buffer_frames;
    u32                 block_frames;
    f32*                scratch;
    f32*                mix;
    Mel_Thread          worker;
    bool                worker_spawned;
    _Atomic(u32)        lost;
    _Atomic(u32)        want_running;
    _Atomic(void*)      opens;
    Mel_Array(void*) garbage;
} Engine;

typedef struct
{
    str8                     stable_id;
    str8                     name;
    IMMDevice*               device;
    IAudioEndpointVolume*    vol;
    bool                     vol_notify;
    const mel_audioout_kind* kind;
    u32                      channels;
    u32                      samplerate;
    f32                      volume;
    bool                     muted;
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
    str8                  default_id;
    Mel_AudioOut_Provider provider;
} Wasapi_Out;

static Wasapi_Out g_out;

typedef struct
{
    IMMNotificationClientVtbl* lpVtbl;
    _Atomic(LONG)              refs;
} Wasapi_Out_Notify;

static Wasapi_Out_Notify g_out_notify;

typedef struct
{
    IAudioEndpointVolumeCallbackVtbl* lpVtbl;
    _Atomic(LONG)                     refs;
} Wasapi_Out_Vol_Notify;

static Wasapi_Out_Vol_Notify g_out_vol_notify;

static void wasapi_log_hr(const char* what, HRESULT hr) { mel_log_error("audioout", "%s failed (hr=0x%08lx)", what, (unsigned long)hr); }

static void wasapi_str8_free(str8* s)
{
    if (s->data != NULL)
        mel_dealloc(g_out.alloc, s->data);
    *s = STR8_EMPTY;
}

static str8 wasapi_utf8_from_wide(const WCHAR* w)
{
    if (w == NULL)
        return STR8_EMPTY;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0)
        return STR8_EMPTY;
    u8* buf = (u8*)mel_alloc(g_out.alloc, (usize)bytes);
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
        atomic_fetch_add_explicit(&g_out_notify.refs, 1, memory_order_relaxed);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE wasapi_notify_addref(IMMNotificationClient* self)
{
    MEL_UNUSED(self);
    return (ULONG)(atomic_fetch_add_explicit(&g_out_notify.refs, 1, memory_order_relaxed) + 1);
}

static ULONG STDMETHODCALLTYPE wasapi_notify_release(IMMNotificationClient* self)
{
    MEL_UNUSED(self);
    return (ULONG)(atomic_fetch_sub_explicit(&g_out_notify.refs, 1, memory_order_relaxed) - 1);
}

static HRESULT STDMETHODCALLTYPE wasapi_notify_state_changed(IMMNotificationClient* self, LPCWSTR id, DWORD state)
{
    MEL_UNUSED(self);
    MEL_UNUSED(id);
    MEL_UNUSED(state);
    mel_audioout_provider_notify(g_out.provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wasapi_notify_device_added(IMMNotificationClient* self, LPCWSTR id)
{
    MEL_UNUSED(self);
    MEL_UNUSED(id);
    mel_audioout_provider_notify(g_out.provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wasapi_notify_device_removed(IMMNotificationClient* self, LPCWSTR id)
{
    MEL_UNUSED(self);
    MEL_UNUSED(id);
    mel_audioout_provider_notify(g_out.provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wasapi_notify_default_changed(IMMNotificationClient* self, EDataFlow flow, ERole role, LPCWSTR id)
{
    MEL_UNUSED(self);
    MEL_UNUSED(id);
    if (flow == eRender && role == eConsole)
        mel_audioout_provider_notify(g_out.provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE wasapi_notify_property_changed(IMMNotificationClient* self, LPCWSTR id, const PROPERTYKEY key)
{
    MEL_UNUSED(self);
    MEL_UNUSED(id);
    MEL_UNUSED(key);
    return S_OK;
}

static IMMNotificationClientVtbl g_out_notify_vtbl = {
    .QueryInterface = wasapi_notify_query,
    .AddRef = wasapi_notify_addref,
    .Release = wasapi_notify_release,
    .OnDeviceStateChanged = wasapi_notify_state_changed,
    .OnDeviceAdded = wasapi_notify_device_added,
    .OnDeviceRemoved = wasapi_notify_device_removed,
    .OnDefaultDeviceChanged = wasapi_notify_default_changed,
    .OnPropertyValueChanged = wasapi_notify_property_changed,
};

static HRESULT STDMETHODCALLTYPE wasapi_vol_query(IAudioEndpointVolumeCallback* self, REFIID riid, void** out)
{
    if (out == NULL)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAudioEndpointVolumeCallback))
    {
        *out = self;
        atomic_fetch_add_explicit(&g_out_vol_notify.refs, 1, memory_order_relaxed);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE wasapi_vol_addref(IAudioEndpointVolumeCallback* self)
{
    MEL_UNUSED(self);
    return (ULONG)(atomic_fetch_add_explicit(&g_out_vol_notify.refs, 1, memory_order_relaxed) + 1);
}

static ULONG STDMETHODCALLTYPE wasapi_vol_release(IAudioEndpointVolumeCallback* self)
{
    MEL_UNUSED(self);
    return (ULONG)(atomic_fetch_sub_explicit(&g_out_vol_notify.refs, 1, memory_order_relaxed) - 1);
}

static HRESULT STDMETHODCALLTYPE wasapi_vol_on_notify(IAudioEndpointVolumeCallback* self, PAUDIO_VOLUME_NOTIFICATION_DATA data)
{
    MEL_UNUSED(self);
    MEL_UNUSED(data);
    mel_audioout_provider_notify(g_out.provider);
    return S_OK;
}

static IAudioEndpointVolumeCallbackVtbl g_out_vol_notify_vtbl = {
    .QueryInterface = wasapi_vol_query,
    .AddRef = wasapi_vol_addref,
    .Release = wasapi_vol_release,
    .OnNotify = wasapi_vol_on_notify,
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

static Device_Rec* wasapi_device_find(str8 stable_id)
{
    for (usize i = 0; i < g_out.devices.count; i++)
        if (str8_equals(g_out.devices.items[i].stable_id, stable_id))
            return &g_out.devices.items[i];
    return NULL;
}

static Engine* wasapi_engine_find(str8 stable_id)
{
    for (usize i = 0; i < g_out.engines.count; i++)
        if (str8_equals(g_out.engines.items[i]->stable_id, stable_id))
            return g_out.engines.items[i];
    return NULL;
}

static void wasapi_device_rec_free(Device_Rec* rec)
{
    wasapi_str8_free(&rec->stable_id);
    wasapi_str8_free(&rec->name);
    if (rec->vol != NULL)
    {
        if (rec->vol_notify)
        {
            HRESULT hr = IAudioEndpointVolume_UnregisterControlChangeNotify(rec->vol, (IAudioEndpointVolumeCallback*)&g_out_vol_notify);
            if (FAILED(hr))
                wasapi_log_hr("IAudioEndpointVolume::UnregisterControlChangeNotify", hr);
            rec->vol_notify = false;
        }
        IAudioEndpointVolume_Release(rec->vol);
        rec->vol = NULL;
    }
    if (rec->device != NULL)
    {
        IMMDevice_Release(rec->device);
        rec->device = NULL;
    }
}

static void wasapi_devices_clear(void)
{
    for (usize i = 0; i < g_out.devices.count; i++)
        wasapi_device_rec_free(&g_out.devices.items[i]);
    mel_array_clear(&g_out.devices);
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

static const mel_audioout_kind* wasapi_device_kind(IPropertyStore* props)
{
    if (props == NULL)
        return &mel_audioout_unknown;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    const mel_audioout_kind* kind = &mel_audioout_unknown;
    if (SUCCEEDED(IPropertyStore_GetValue(props, &PKEY_AudioEndpoint_FormFactor, &pv)) && pv.vt == VT_UI4)
    {
        if (pv.ulVal == Speakers || pv.ulVal == Headphones || pv.ulVal == Headset)
            kind = &mel_audioout_builtin;
        else if (pv.ulVal == DigitalAudioDisplayDevice)
            kind = &mel_audioout_hdmi;
    }
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

static void wasapi_device_volume_attach(Device_Rec* rec)
{
    IAudioEndpointVolume* vol = NULL;
    HRESULT               hr = IMMDevice_Activate(rec->device, &IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&vol);
    if (FAILED(hr) || vol == NULL)
        return;

    float v = 0.0f;
    BOOL  m = FALSE;
    if (FAILED(IAudioEndpointVolume_GetMasterVolumeLevelScalar(vol, &v)) || FAILED(IAudioEndpointVolume_GetMute(vol, &m)))
    {
        mel_log_warn("audioout", "endpoint volume on %.*s activates but does not answer; no volume capability", (int)rec->stable_id.len, rec->stable_id.data);
        IAudioEndpointVolume_Release(vol);
        return;
    }
    rec->vol = vol;
    rec->volume = (f32)v;
    rec->muted = m != FALSE;

    hr = IAudioEndpointVolume_RegisterControlChangeNotify(vol, (IAudioEndpointVolumeCallback*)&g_out_vol_notify);
    if (FAILED(hr))
    {
        wasapi_log_hr("IAudioEndpointVolume::RegisterControlChangeNotify", hr);
        mel_log_warn("audioout", "external volume changes on %.*s will not surface as events", (int)rec->stable_id.len, rec->stable_id.data);
        return;
    }
    rec->vol_notify = true;
}

static void wasapi_device_rec_build(IMMDevice* dev)
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
    rec.stable_id = str8_fmt(g_out.alloc, "wasapi:%.*s", (int)idu.len, idu.data);
    mel_dealloc(g_out.alloc, idu.data);

    if (!wasapi_device_mix_format(dev, &rec.channels, &rec.samplerate))
    {
        mel_log_warn("audioout", "skipping endpoint %.*s: mix format unavailable", (int)rec.stable_id.len, rec.stable_id.data);
        wasapi_device_rec_free(&rec);
        return;
    }

    IPropertyStore* props = NULL;
    if (FAILED(IMMDevice_OpenPropertyStore(dev, STGM_READ, &props)))
        props = NULL;
    rec.name = wasapi_device_friendly_name(props);
    rec.kind = wasapi_device_kind(props);
    if (props != NULL)
        IPropertyStore_Release(props);

    if (rec.name.data == NULL)
    {
        mel_log_warn("audioout", "endpoint %.*s has no friendly name; using stable id", (int)rec.stable_id.len, rec.stable_id.data);
        rec.name = str8_dup(rec.stable_id, g_out.alloc);
    }

    wasapi_device_volume_attach(&rec);

    mel_array_push(&g_out.devices, rec);
}

static void wasapi_enumerate(void* user, Mel_AudioOut_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    wasapi_devices_clear();
    if (g_out.enumerator == NULL)
    {
        mel_log_error("audioout", "wasapi enumerate without a device enumerator; COM setup failed at registration");
        return;
    }

    IMMDeviceCollection* coll = NULL;
    HRESULT              hr = IMMDeviceEnumerator_EnumAudioEndpoints(g_out.enumerator, eRender, DEVICE_STATE_ACTIVE, &coll);
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
        wasapi_device_rec_build(dev);
    }
    IMMDeviceCollection_Release(coll);

    for (usize i = 0; i < g_out.devices.count; i++)
    {
        Device_Rec*      rec = &g_out.devices.items[i];
        Mel_AudioOut_Raw raw = {
            .stable_id = rec->stable_id,
            .name = rec->name,
            .kind = rec->kind,
            .channels = rec->channels,
            .samplerate = rec->samplerate,
            .samplerates = &rec->samplerate,
            .samplerate_count = 1,
            .caps = { .volume = rec->vol != NULL, .mute = rec->vol != NULL },
            .volume = rec->volume,
            .muted = rec->muted,
        };
        if (!fn(&raw, fn_user))
            return;
    }
}

static str8 wasapi_default_id(void* user)
{
    MEL_UNUSED(user);
    if (g_out.enumerator == NULL)
        return STR8_EMPTY;
    IMMDevice* dev = NULL;
    HRESULT    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(g_out.enumerator, eRender, eConsole, &dev);
    if (FAILED(hr) || dev == NULL)
    {
        mel_log_warn("audioout", "GetDefaultAudioEndpoint(eRender) failed (hr=0x%08lx); no default output", (unsigned long)hr);
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
    wasapi_str8_free(&g_out.default_id);
    g_out.default_id = str8_fmt(g_out.alloc, "wasapi:%.*s", (int)idu.len, idu.data);
    mel_dealloc(g_out.alloc, idu.data);
    return g_out.default_id;
}

static bool wasapi_engine_fill(Engine* e, u32 frames, bool* lost)
{
    if (frames == 0u)
        return true;

    BYTE*   data = NULL;
    HRESULT hr = IAudioRenderClient_GetBuffer(e->render, frames, &data);
    if (FAILED(hr) || data == NULL)
    {
        *lost = hr == AUDCLNT_E_DEVICE_INVALIDATED;
        wasapi_log_hr("IAudioRenderClient::GetBuffer", hr);
        return false;
    }

    u32  samples = frames * e->channels;
    f32* acc = e->fmt_float ? (f32*)data : e->mix;
    memset(acc, 0, sizeof(f32) * (usize)samples);

    u32        produced = 0;
    Open_List* ol = atomic_load_explicit(&e->opens, memory_order_acquire);
    if (ol != NULL)
    {
        for (u32 i = 0; i < ol->count; i++)
        {
            if (!ol->opens[i].started)
                continue;
            u32 got = ol->opens[i].src.pull(ol->opens[i].src.token, e->scratch, frames);
            if (got > frames)
                got = frames;
            u32 n = got * e->channels;
            for (u32 s = 0; s < n; s++)
                acc[s] += e->scratch[s];
            if (got > produced)
                produced = got;
        }
    }

    DWORD flags = 0u;
    if (produced == 0u)
    {
        flags = AUDCLNT_BUFFERFLAGS_SILENT;
    }
    else if (!e->fmt_float)
    {
        i16* dst = (i16*)data;
        for (u32 s = 0; s < samples; s++)
        {
            f32 v = acc[s];
            if (v > 1.0f)
                v = 1.0f;
            if (v < -1.0f)
                v = -1.0f;
            dst[s] = (i16)(v * 32767.0f);
        }
    }

    hr = IAudioRenderClient_ReleaseBuffer(e->render, frames, flags);
    if (FAILED(hr))
    {
        *lost = hr == AUDCLNT_E_DEVICE_INVALIDATED;
        wasapi_log_hr("IAudioRenderClient::ReleaseBuffer", hr);
        return false;
    }
    return true;
}

static void wasapi_engine_fire_lost(Engine* e)
{
    if (atomic_exchange_explicit(&e->lost, 1u, memory_order_acq_rel) != 0u)
        return;
    Open_List* ol = atomic_load_explicit(&e->opens, memory_order_acquire);
    if (ol == NULL)
        return;
    for (u32 i = 0; i < ol->count; i++)
        if (ol->opens[i].src.on_lost)
            ol->opens[i].src.on_lost(ol->opens[i].src.token);
}

static int wasapi_engine_worker(void* user)
{
    Engine* e = user;

    HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool    co_owned = !FAILED(co) && co != RPC_E_CHANGED_MODE;

    HANDLE waits[3] = { e->stop_event, e->wake_event, e->audio_event };
    bool   running = false;
    bool   lost = false;
    for (;;)
    {
        DWORD w = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0)
            break;
        if (w != WAIT_OBJECT_0 + 1 && w != WAIT_OBJECT_0 + 2)
        {
            mel_log_error("audioout", "render wait returned %lu (err=%lu)", (unsigned long)w, (unsigned long)GetLastError());
            break;
        }

        u32 want = atomic_load_explicit(&e->want_running, memory_order_acquire);
        if (want != 0u && !running)
        {
            UINT32  pad = 0;
            HRESULT hr = IAudioClient_GetCurrentPadding(e->client, &pad);
            if (FAILED(hr))
            {
                lost = hr == AUDCLNT_E_DEVICE_INVALIDATED;
                wasapi_log_hr("IAudioClient::GetCurrentPadding", hr);
                break;
            }
            if (!wasapi_engine_fill(e, e->buffer_frames - pad, &lost))
                break;
            hr = IAudioClient_Start(e->client);
            if (FAILED(hr))
            {
                lost = hr == AUDCLNT_E_DEVICE_INVALIDATED;
                wasapi_log_hr("IAudioClient::Start", hr);
                break;
            }
            running = true;
            continue;
        }
        if (want == 0u && running)
        {
            IAudioClient_Stop(e->client);
            IAudioClient_Reset(e->client);
            running = false;
            continue;
        }
        if (!running)
            continue;

        UINT32  pad = 0;
        HRESULT hr = IAudioClient_GetCurrentPadding(e->client, &pad);
        if (FAILED(hr))
        {
            lost = hr == AUDCLNT_E_DEVICE_INVALIDATED;
            wasapi_log_hr("IAudioClient::GetCurrentPadding", hr);
            break;
        }
        if (e->buffer_frames > pad && !wasapi_engine_fill(e, e->buffer_frames - pad, &lost))
            break;
    }

    if (running)
    {
        IAudioClient_Stop(e->client);
        IAudioClient_Reset(e->client);
    }
    if (lost)
    {
        mel_log_error("audioout", "render device invalidated: %.*s; engine wound down", (int)e->stable_id.len, e->stable_id.data);
        wasapi_engine_fire_lost(e);
        mel_audioout_provider_notify(g_out.provider);
    }
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
    Open_List* ol = atomic_exchange_explicit(&e->opens, NULL, memory_order_acq_rel);
    if (ol != NULL)
        mel_dealloc(g_out.alloc, ol);
    for (usize i = 0; i < e->garbage.count; i++)
        mel_dealloc(g_out.alloc, e->garbage.items[i]);
    mel_array_free(&e->garbage);
    if (e->render != NULL)
        IAudioRenderClient_Release(e->render);
    if (e->client != NULL)
        IAudioClient_Release(e->client);
    if (e->device != NULL)
        IMMDevice_Release(e->device);
    if (e->audio_event != NULL)
        CloseHandle(e->audio_event);
    if (e->wake_event != NULL)
        CloseHandle(e->wake_event);
    if (e->stop_event != NULL)
        CloseHandle(e->stop_event);
    if (e->scratch != NULL)
        mel_dealloc(g_out.alloc, e->scratch);
    if (e->mix != NULL)
        mel_dealloc(g_out.alloc, e->mix);
    wasapi_str8_free(&e->stable_id);
    mel_dealloc(g_out.alloc, e);
}

static void wasapi_engine_opens_swap(Engine* e, Open_List* nl)
{
    void* old = atomic_exchange_explicit(&e->opens, nl, memory_order_acq_rel);
    if (old != NULL)
        mel_array_push(&e->garbage, old);
}

static Open_List* wasapi_engine_opens_clone(Engine* e, u32 extra)
{
    Open_List* cur = atomic_load_explicit(&e->opens, memory_order_acquire);
    u32        count = cur != NULL ? cur->count : 0u;
    Open_List* nl = mel_alloc(g_out.alloc, sizeof(Open_List) + sizeof(Open_Rec) * ((usize)count + extra));
    if (nl == NULL)
        return NULL;
    for (u32 i = 0; i < count; i++)
        nl->opens[i] = cur->opens[i];
    nl->count = count;
    return nl;
}

static bool wasapi_engine_open_add(Engine* e, Mel_AudioOut_Source src)
{
    Open_List* nl = wasapi_engine_opens_clone(e, 1);
    if (nl == NULL)
        return false;
    nl->opens[nl->count] = (Open_Rec){ .src = src, .started = false };
    nl->count++;
    wasapi_engine_opens_swap(e, nl);
    return true;
}

static void wasapi_engine_update_running(Engine* e)
{
    Open_List* ol = atomic_load_explicit(&e->opens, memory_order_acquire);
    u32        started = 0;
    if (ol != NULL)
        for (u32 i = 0; i < ol->count; i++)
            if (ol->opens[i].started)
                started++;
    atomic_store_explicit(&e->want_running, started > 0u ? 1u : 0u, memory_order_release);
    SetEvent(e->wake_event);
}

static Engine* wasapi_engine_create(Device_Rec* rec, Mel_AudioOut_Source src, Mel_AudioOut_Status* status)
{
    *status = MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    Engine* e = mel_alloc_type(g_out.alloc, Engine);
    if (e == NULL)
        return NULL;
    memset(e, 0, sizeof *e);
    mel_array_init(&e->garbage, g_out.alloc);
    e->stable_id = str8_dup(rec->stable_id, g_out.alloc);
    e->device = rec->device;
    IMMDevice_AddRef(e->device);

    HRESULT hr = IMMDevice_Activate(e->device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&e->client);
    if (FAILED(hr) || e->client == NULL)
    {
        wasapi_log_hr("IMMDevice::Activate(IAudioClient)", hr);
        wasapi_engine_free(e);
        return NULL;
    }

    WAVEFORMATEX* mix = NULL;
    hr = IAudioClient_GetMixFormat(e->client, &mix);
    if (FAILED(hr) || mix == NULL)
    {
        wasapi_log_hr("IAudioClient::GetMixFormat", hr);
        wasapi_engine_free(e);
        return NULL;
    }

    if (wasapi_format_is_float32(mix))
        e->fmt_float = true;
    else if (wasapi_format_is_pcm16(mix))
        e->fmt_float = false;
    else
    {
        mel_log_error("audioout", "mix format unsupported for render (tag=%u bits=%u) on %.*s", (u32)mix->wFormatTag, (u32)mix->wBitsPerSample, (int)e->stable_id.len, e->stable_id.data);
        CoTaskMemFree(mix);
        wasapi_engine_free(e);
        return NULL;
    }
    e->channels = (u32)mix->nChannels;
    e->samplerate = (u32)mix->nSamplesPerSec;

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

    hr = IAudioClient_Initialize(e->client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, default_period, 0, mix, NULL);
    CoTaskMemFree(mix);
    if (FAILED(hr))
    {
        wasapi_log_hr("IAudioClient::Initialize(SHARED,EVENTCALLBACK)", hr);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
            *status = MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_LOST;
        else if (hr == AUDCLNT_E_DEVICE_IN_USE)
            *status = MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_BUSY;
        wasapi_engine_free(e);
        return NULL;
    }

    e->audio_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (e->audio_event == NULL)
    {
        mel_log_error("audioout", "CreateEventW(audio) failed (err=%lu)", (unsigned long)GetLastError());
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
    e->block_frames = (u32)(((i64)default_period * (i64)e->samplerate) / MEL_AUDIOOUT_WASAPI_REFTIMES_PER_SEC);
    if (e->block_frames == 0u || e->block_frames > e->buffer_frames)
    {
        mel_log_warn("audioout", "device period of %.*s maps to %u frames; granting buffer size %u", (int)e->stable_id.len, e->stable_id.data, e->block_frames, e->buffer_frames);
        e->block_frames = e->buffer_frames;
    }

    hr = IAudioClient_GetService(e->client, &IID_IAudioRenderClient, (void**)&e->render);
    if (FAILED(hr) || e->render == NULL)
    {
        wasapi_log_hr("IAudioClient::GetService(IAudioRenderClient)", hr);
        wasapi_engine_free(e);
        return NULL;
    }

    e->scratch = mel_alloc(g_out.alloc, sizeof(f32) * (usize)e->buffer_frames * e->channels);
    if (e->scratch == NULL)
    {
        wasapi_engine_free(e);
        return NULL;
    }
    if (!e->fmt_float)
    {
        e->mix = mel_alloc(g_out.alloc, sizeof(f32) * (usize)e->buffer_frames * e->channels);
        if (e->mix == NULL)
        {
            wasapi_engine_free(e);
            return NULL;
        }
    }

    e->wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (e->wake_event == NULL)
    {
        mel_log_error("audioout", "CreateEventW(wake) failed (err=%lu)", (unsigned long)GetLastError());
        wasapi_engine_free(e);
        return NULL;
    }
    e->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (e->stop_event == NULL)
    {
        mel_log_error("audioout", "CreateEventW(stop) failed (err=%lu)", (unsigned long)GetLastError());
        wasapi_engine_free(e);
        return NULL;
    }

    if (!wasapi_engine_open_add(e, src))
    {
        wasapi_engine_free(e);
        return NULL;
    }

    if (!mel_thread_spawn(&e->worker, wasapi_engine_worker, e, .name = "mel-audioout"))
    {
        mel_log_error("audioout", "failed to spawn render worker for %.*s", (int)e->stable_id.len, e->stable_id.data);
        wasapi_engine_free(e);
        return NULL;
    }
    e->worker_spawned = true;

    mel_log_info("audioout", "render engine ready: %.*s %uch @ %uHz (%s, buffer %u frames, block %u)", (int)e->stable_id.len, e->stable_id.data, e->channels, e->samplerate, e->fmt_float ? "f32" : "s16", e->buffer_frames, e->block_frames);
    *status = MEL_AUDIOOUT_OK;
    return e;
}

static Mel_AudioOut_Status wasapi_open(void* user, str8 stable_id, Mel_AudioOut_Format req, Mel_AudioOut_Format* granted, Mel_AudioOut_Source src)
{
    MEL_UNUSED(user);
    MEL_UNUSED(req);
    assert(granted != NULL);
    assert(src.pull != NULL);

    Engine* e = wasapi_engine_find(stable_id);
    if (e != NULL)
    {
        if (atomic_load_explicit(&e->lost, memory_order_acquire) != 0u)
        {
            mel_log_error("audioout", "open on lost render engine %.*s", (int)stable_id.len, stable_id.data);
            return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_LOST;
        }
        if (!wasapi_engine_open_add(e, src))
            return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
        granted->samplerate = e->samplerate;
        granted->channels = e->channels;
        granted->block_frames = e->block_frames;
        return MEL_AUDIOOUT_OK;
    }

    Device_Rec* rec = wasapi_device_find(stable_id);
    if (rec == NULL)
    {
        mel_log_error("audioout", "open on unknown device %.*s", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_NO_DEVICE;
    }

    Mel_AudioOut_Status status;
    e = wasapi_engine_create(rec, src, &status);
    if (e == NULL)
        return status;
    mel_array_push(&g_out.engines, e);
    granted->samplerate = e->samplerate;
    granted->channels = e->channels;
    granted->block_frames = e->block_frames;
    return status;
}

static void wasapi_set_started(str8 stable_id, void* token, bool started, const char* what)
{
    Engine* e = wasapi_engine_find(stable_id);
    if (e == NULL)
    {
        mel_log_error("audioout", "%s on device without render engine: %.*s", what, (int)stable_id.len, stable_id.data);
        return;
    }
    Open_List* nl = wasapi_engine_opens_clone(e, 0);
    if (nl == NULL)
        return;
    bool found = false;
    for (u32 i = 0; i < nl->count; i++)
    {
        if (nl->opens[i].src.token == token)
        {
            nl->opens[i].started = started;
            found = true;
        }
    }
    if (!found)
    {
        mel_log_error("audioout", "%s with unknown token on %.*s", what, (int)stable_id.len, stable_id.data);
        mel_dealloc(g_out.alloc, nl);
        return;
    }
    wasapi_engine_opens_swap(e, nl);
    wasapi_engine_update_running(e);
}

static void wasapi_start(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    Engine* e = wasapi_engine_find(stable_id);
    if (e != NULL && atomic_load_explicit(&e->lost, memory_order_acquire) != 0u)
    {
        mel_log_error("audioout", "start on lost render engine %.*s", (int)stable_id.len, stable_id.data);
        return;
    }
    wasapi_set_started(stable_id, token, true, "start");
}

static void wasapi_stop(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    wasapi_set_started(stable_id, token, false, "stop");
}

static void wasapi_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    Engine* e = wasapi_engine_find(stable_id);
    if (e == NULL)
    {
        mel_log_error("audioout", "close on device without render engine: %.*s", (int)stable_id.len, stable_id.data);
        return;
    }

    Open_List* cur = atomic_load_explicit(&e->opens, memory_order_acquire);
    u32        count = cur != NULL ? cur->count : 0u;
    if (count == 0u)
    {
        mel_log_error("audioout", "close with no opens on %.*s", (int)stable_id.len, stable_id.data);
        return;
    }

    Open_List* nl = mel_alloc(g_out.alloc, sizeof(Open_List) + sizeof(Open_Rec) * (usize)count);
    if (nl == NULL)
        return;
    u32 kept = 0;
    for (u32 i = 0; i < count; i++)
        if (cur->opens[i].src.token != token)
            nl->opens[kept++] = cur->opens[i];
    nl->count = kept;
    if (kept == count)
    {
        mel_log_error("audioout", "close with unknown token on %.*s", (int)stable_id.len, stable_id.data);
        mel_dealloc(g_out.alloc, nl);
        return;
    }
    wasapi_engine_opens_swap(e, nl);

    if (kept > 0u)
    {
        wasapi_engine_update_running(e);
        return;
    }

    for (usize i = 0; i < g_out.engines.count; i++)
    {
        if (g_out.engines.items[i] == e)
        {
            g_out.engines.items[i] = g_out.engines.items[g_out.engines.count - 1];
            g_out.engines.count--;
            break;
        }
    }
    mel_log_info("audioout", "render engine stopped: %.*s", (int)stable_id.len, stable_id.data);
    wasapi_engine_free(e);
}

static IAudioEndpointVolume* wasapi_volume_iface(str8 stable_id, const char* what)
{
    Device_Rec* rec = wasapi_device_find(stable_id);
    if (rec == NULL || rec->vol == NULL)
    {
        mel_log_error("audioout", "%s on device without volume capability: %.*s", what, (int)stable_id.len, stable_id.data);
        return NULL;
    }
    return rec->vol;
}

static f32 wasapi_volume(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    IAudioEndpointVolume* vol = wasapi_volume_iface(stable_id, "volume");
    if (vol == NULL)
        return 0.0f;
    float   v = 0.0f;
    HRESULT hr = IAudioEndpointVolume_GetMasterVolumeLevelScalar(vol, &v);
    if (FAILED(hr))
    {
        wasapi_log_hr("IAudioEndpointVolume::GetMasterVolumeLevelScalar", hr);
        return 0.0f;
    }
    return (f32)v;
}

static Mel_AudioOut_Status wasapi_set_volume(void* user, str8 stable_id, f32 volume)
{
    MEL_UNUSED(user);
    IAudioEndpointVolume* vol = wasapi_volume_iface(stable_id, "set_volume");
    if (vol == NULL)
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    HRESULT hr = IAudioEndpointVolume_SetMasterVolumeLevelScalar(vol, (float)volume, NULL);
    if (FAILED(hr))
    {
        wasapi_log_hr("IAudioEndpointVolume::SetMasterVolumeLevelScalar", hr);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    return MEL_AUDIOOUT_OK;
}

static bool wasapi_muted(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    IAudioEndpointVolume* vol = wasapi_volume_iface(stable_id, "muted");
    if (vol == NULL)
        return false;
    BOOL    m = FALSE;
    HRESULT hr = IAudioEndpointVolume_GetMute(vol, &m);
    if (FAILED(hr))
    {
        wasapi_log_hr("IAudioEndpointVolume::GetMute", hr);
        return false;
    }
    return m != FALSE;
}

static Mel_AudioOut_Status wasapi_set_muted(void* user, str8 stable_id, bool muted)
{
    MEL_UNUSED(user);
    IAudioEndpointVolume* vol = wasapi_volume_iface(stable_id, "set_muted");
    if (vol == NULL)
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    HRESULT hr = IAudioEndpointVolume_SetMute(vol, muted ? TRUE : FALSE, NULL);
    if (FAILED(hr))
    {
        wasapi_log_hr("IAudioEndpointVolume::SetMute", hr);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    return MEL_AUDIOOUT_OK;
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
    for (usize i = 0; i < g_out.engines.count; i++)
    {
        mel_log_warn("audioout", "render engine %.*s still live at shutdown; releasing", (int)g_out.engines.items[i]->stable_id.len, g_out.engines.items[i]->stable_id.data);
        wasapi_engine_free(g_out.engines.items[i]);
    }
    mel_array_free(&g_out.engines);

    if (g_out.notify_registered && g_out.enumerator != NULL)
    {
        HRESULT hr = IMMDeviceEnumerator_UnregisterEndpointNotificationCallback(g_out.enumerator, (IMMNotificationClient*)&g_out_notify);
        if (FAILED(hr))
            wasapi_log_hr("IMMDeviceEnumerator::UnregisterEndpointNotificationCallback", hr);
        g_out.notify_registered = false;
    }

    wasapi_devices_clear();
    mel_array_free(&g_out.devices);
    wasapi_str8_free(&g_out.default_id);

    if (g_out.enumerator != NULL)
    {
        IMMDeviceEnumerator_Release(g_out.enumerator);
        g_out.enumerator = NULL;
    }
    if (g_out.com_owned)
        CoUninitialize();
    memset(&g_out, 0, sizeof g_out);
}

static const Mel_AudioOut_Provider_Desc WASAPI_DESC = {
    .name = "win32-wasapi",
    .enumerate = wasapi_enumerate,
    .default_id = wasapi_default_id,
    .open = wasapi_open,
    .start = wasapi_start,
    .stop = wasapi_stop,
    .close = wasapi_close,
    .volume = wasapi_volume,
    .set_volume = wasapi_set_volume,
    .muted = wasapi_muted,
    .set_muted = wasapi_set_muted,
    .native = wasapi_native,
    .shutdown = wasapi_shutdown,
};

void mel_audioout__register_host_providers(void)
{
    assert(!g_out.registered);
    g_out.alloc = mel_alloc_heap();
    mel_array_init(&g_out.devices, g_out.alloc);
    mel_array_init(&g_out.engines, g_out.alloc);
    g_out.default_id = STR8_EMPTY;
    g_out.provider = mel_audioout_provider_register(&WASAPI_DESC);
    g_out.registered = true;

    g_out_vol_notify.lpVtbl = &g_out_vol_notify_vtbl;
    atomic_store_explicit(&g_out_vol_notify.refs, 1, memory_order_relaxed);

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE)
    {
        mel_log_warn("audioout", "COM already initialized in a different apartment; not owning teardown");
    }
    else if (FAILED(hr))
    {
        wasapi_log_hr("CoInitializeEx", hr);
        return;
    }
    else
    {
        g_out.com_owned = true;
    }

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&g_out.enumerator);
    if (FAILED(hr))
    {
        wasapi_log_hr("CoCreateInstance(MMDeviceEnumerator)", hr);
        g_out.enumerator = NULL;
        return;
    }

    g_out_notify.lpVtbl = &g_out_notify_vtbl;
    atomic_store_explicit(&g_out_notify.refs, 1, memory_order_relaxed);
    hr = IMMDeviceEnumerator_RegisterEndpointNotificationCallback(g_out.enumerator, (IMMNotificationClient*)&g_out_notify);
    if (FAILED(hr))
    {
        wasapi_log_hr("IMMDeviceEnumerator::RegisterEndpointNotificationCallback", hr);
        return;
    }
    g_out.notify_registered = true;
}
