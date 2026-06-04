#include <audio/backend.h>

#include <core/types.h>
#include <allocator/allocator.h>
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

#include <stdatomic.h>

u32 mel_audio_ring_read_available(const Mel_Audio_Ring* r);
u32 mel_audio_ring_read(Mel_Audio_Ring* r, f32* dst, u32 count);

#define MEL_WASAPI__REFTIMES_PER_SEC 10000000LL

typedef struct
{
    const Mel_Alloc*     alloc;
    IMMDeviceEnumerator* enumerator;
    IMMDevice*           device;
    IAudioClient*        client;
    IAudioRenderClient*  render;
    WAVEFORMATEX*        format;
    HANDLE               audio_event;
    HANDLE               stop_event;

    u32 channels;
    u32 samplerate;
    u32 buffer_frames;

    Mel_Audio_Ring* ring;
    Mel_Thread      worker;
    u32             worker_spawned;
    _Atomic(u32)    underruns;
    _Atomic(u32)    primed;
    _Atomic(u32)    com_owned;
} Mel_Wasapi;

static Mel_Wasapi mel_wasapi__state;

static void mel_wasapi__log_hr(const char* what, HRESULT hr)
{
    mel_log_error("audio.wasapi", "%s failed (hr=0x%08lx)", what, (unsigned long)hr);
}

static bool mel_wasapi__build_float_format(Mel_Wasapi* s, u32 want_rate, u32 want_channels)
{
    WAVEFORMATEX* mix = NULL;
    HRESULT       hr = IAudioClient_GetMixFormat(s->client, &mix);
    if (FAILED(hr) || mix == NULL)
    {
        mel_wasapi__log_hr("IAudioClient::GetMixFormat", hr);
        return false;
    }

    u32 rate = want_rate > 0u ? want_rate : (u32)mix->nSamplesPerSec;
    u32 channels = want_channels > 0u ? want_channels : (u32)mix->nChannels;
    CoTaskMemFree(mix);

    WAVEFORMATEXTENSIBLE* ext = mel_calloc(s->alloc, sizeof(WAVEFORMATEXTENSIBLE));
    if (ext == NULL)
        return false;

    ext->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    ext->Format.nChannels = (WORD)channels;
    ext->Format.nSamplesPerSec = (DWORD)rate;
    ext->Format.wBitsPerSample = 32u;
    ext->Format.nBlockAlign = (WORD)(channels * 4u);
    ext->Format.nAvgBytesPerSec = (DWORD)(rate * channels * 4u);
    ext->Format.cbSize = (WORD)(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
    ext->Samples.wValidBitsPerSample = 32u;
    ext->dwChannelMask = channels == 1u ? KSAUDIO_SPEAKER_MONO : KSAUDIO_SPEAKER_STEREO;
    ext->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    WAVEFORMATEX* closest = NULL;
    hr = IAudioClient_IsFormatSupported(s->client, AUDCLNT_SHAREMODE_SHARED, (WAVEFORMATEX*)ext, &closest);
    if (hr == S_FALSE && closest != NULL)
    {
        mel_log_warn("audio.wasapi", "requested %uHz %uch float not exact; AUTOCONVERTPCM bridges to device mix", rate, channels);
        CoTaskMemFree(closest);
    }
    else if (FAILED(hr))
    {
        mel_wasapi__log_hr("IAudioClient::IsFormatSupported", hr);
        if (closest != NULL)
            CoTaskMemFree(closest);
        mel_dealloc(s->alloc, ext);
        return false;
    }

    s->format = (WAVEFORMATEX*)ext;
    s->channels = channels;
    s->samplerate = rate;
    return true;
}

bool mel_audio_backend_open(Mel_Audio_Opt req, Mel_Audio_Caps* granted, const Mel_Alloc* a)
{
    assert(granted != NULL);
    assert(a != NULL);

    Mel_Wasapi* s = &mel_wasapi__state;
    *s = (Mel_Wasapi){ 0 };
    s->alloc = a;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE)
    {
        mel_log_warn("audio.wasapi", "COM already initialized in a different apartment; not owning teardown");
    }
    else if (FAILED(hr))
    {
        mel_wasapi__log_hr("CoInitializeEx", hr);
        return false;
    }
    else
    {
        atomic_store_explicit(&s->com_owned, 1u, memory_order_relaxed);
    }

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&s->enumerator);
    if (FAILED(hr))
    {
        mel_wasapi__log_hr("CoCreateInstance(MMDeviceEnumerator)", hr);
        mel_audio_backend_close(a);
        return false;
    }

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(s->enumerator, eRender, eConsole, &s->device);
    if (FAILED(hr))
    {
        mel_wasapi__log_hr("GetDefaultAudioEndpoint(eRender)", hr);
        mel_audio_backend_close(a);
        return false;
    }

    hr = IMMDevice_Activate(s->device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&s->client);
    if (FAILED(hr))
    {
        mel_wasapi__log_hr("IMMDevice::Activate(IAudioClient)", hr);
        mel_audio_backend_close(a);
        return false;
    }

    if (!mel_wasapi__build_float_format(s, req.samplerate, req.channels))
    {
        mel_audio_backend_close(a);
        return false;
    }

    REFERENCE_TIME default_period = 0;
    REFERENCE_TIME min_period = 0;
    hr = IAudioClient_GetDevicePeriod(s->client, &default_period, &min_period);
    if (FAILED(hr))
    {
        mel_wasapi__log_hr("IAudioClient::GetDevicePeriod", hr);
        mel_audio_backend_close(a);
        return false;
    }

    REFERENCE_TIME want_period = default_period;
    if (req.block_frames > 0u && s->samplerate > 0u)
    {
        REFERENCE_TIME from_block = (REFERENCE_TIME)((i64)req.block_frames * MEL_WASAPI__REFTIMES_PER_SEC / (i64)s->samplerate);
        if (from_block > want_period)
            want_period = from_block;
    }

    DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
    hr = IAudioClient_Initialize(s->client, AUDCLNT_SHAREMODE_SHARED, stream_flags, want_period, 0, s->format, NULL);
    if (FAILED(hr))
    {
        mel_wasapi__log_hr("IAudioClient::Initialize(SHARED,EVENTCALLBACK|AUTOCONVERTPCM)", hr);
        mel_audio_backend_close(a);
        return false;
    }

    s->audio_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (s->audio_event == NULL)
    {
        mel_log_error("audio.wasapi", "CreateEventW(audio) failed (err=%lu)", (unsigned long)GetLastError());
        mel_audio_backend_close(a);
        return false;
    }

    hr = IAudioClient_SetEventHandle(s->client, s->audio_event);
    if (FAILED(hr))
    {
        mel_wasapi__log_hr("IAudioClient::SetEventHandle", hr);
        mel_audio_backend_close(a);
        return false;
    }

    UINT32 buffer_frames = 0;
    hr = IAudioClient_GetBufferSize(s->client, &buffer_frames);
    if (FAILED(hr) || buffer_frames == 0u)
    {
        mel_wasapi__log_hr("IAudioClient::GetBufferSize", hr);
        mel_audio_backend_close(a);
        return false;
    }
    s->buffer_frames = (u32)buffer_frames;

    hr = IAudioClient_GetService(s->client, &IID_IAudioRenderClient, (void**)&s->render);
    if (FAILED(hr))
    {
        mel_wasapi__log_hr("IAudioClient::GetService(IAudioRenderClient)", hr);
        mel_audio_backend_close(a);
        return false;
    }

    u32 block_frames = req.block_frames > 0u ? req.block_frames : s->buffer_frames;
    assert(req.ring_blocks > 0u);
    u32 ring_blocks = req.ring_blocks;

    granted->samplerate = s->samplerate;
    granted->channels = s->channels;
    granted->block_frames = block_frames;
    granted->ring_blocks = ring_blocks;
    granted->latency_frames = s->buffer_frames;

    mel_log_info("audio.wasapi", "device opened: %uHz %uch float, buffer %u frames, block %u, ring %u blocks", s->samplerate, s->channels, s->buffer_frames, block_frames, ring_blocks);
    return true;
}

static void mel_wasapi__fill_buffer(Mel_Wasapi* s, UINT32 frames)
{
    if (frames == 0u)
        return;

    BYTE*   data = NULL;
    HRESULT hr = IAudioRenderClient_GetBuffer(s->render, frames, &data);
    if (FAILED(hr) || data == NULL)
    {
        mel_wasapi__log_hr("IAudioRenderClient::GetBuffer", hr);
        return;
    }

    u32 want = frames * s->channels;
    u32 have = mel_audio_ring_read_available(s->ring);
    DWORD flags = 0u;
    if (have > 0u)
        atomic_store_explicit(&s->primed, 1u, memory_order_relaxed);
    if (have < want)
    {
        if (atomic_load_explicit(&s->primed, memory_order_relaxed) != 0u)
        {
            u32 prev = atomic_fetch_add_explicit(&s->underruns, 1u, memory_order_relaxed);
            assert(prev != 0 && "audio.wasapi: ring underrun");
        }
        if (have == 0u)
            flags = AUDCLNT_BUFFERFLAGS_SILENT;
    }

    if (flags == 0u)
        mel_audio_ring_read(s->ring, (f32*)data, want);

    IAudioRenderClient_ReleaseBuffer(s->render, frames, flags);
}

static int mel_wasapi__worker(void* user)
{
    Mel_Wasapi* s = (Mel_Wasapi*)user;

    HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool    co_owned = !FAILED(co) && co != RPC_E_CHANGED_MODE;

    UINT32  pad = 0;
    HRESULT hr = IAudioClient_GetCurrentPadding(s->client, &pad);
    if (SUCCEEDED(hr))
        mel_wasapi__fill_buffer(s, s->buffer_frames - pad);

    hr = IAudioClient_Start(s->client);
    if (FAILED(hr))
    {
        mel_wasapi__log_hr("IAudioClient::Start", hr);
        if (co_owned)
            CoUninitialize();
        return 1;
    }

    HANDLE waits[2] = { s->stop_event, s->audio_event };
    for (;;)
    {
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0)
            break;
        if (w != WAIT_OBJECT_0 + 1)
        {
            mel_log_error("audio.wasapi", "WaitForMultipleObjects returned %lu (err=%lu)", (unsigned long)w, (unsigned long)GetLastError());
            break;
        }

        pad = 0;
        hr = IAudioClient_GetCurrentPadding(s->client, &pad);
        if (FAILED(hr))
        {
            mel_wasapi__log_hr("IAudioClient::GetCurrentPadding", hr);
            break;
        }
        if (s->buffer_frames > pad)
            mel_wasapi__fill_buffer(s, s->buffer_frames - pad);
    }

    IAudioClient_Stop(s->client);
    IAudioClient_Reset(s->client);
    if (co_owned)
        CoUninitialize();
    return 0;
}

void mel_audio_backend_start(Mel_Audio_Ring* ring)
{
    assert(ring != NULL);
    Mel_Wasapi* s = &mel_wasapi__state;
    assert(s->client != NULL && "wasapi backend_start before open");

    s->ring = ring;
    atomic_store_explicit(&s->underruns, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->primed, 0u, memory_order_relaxed);

    s->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (s->stop_event == NULL)
    {
        mel_log_error("audio.wasapi", "CreateEventW(stop) failed (err=%lu)", (unsigned long)GetLastError());
        return;
    }

    if (!mel_thread_spawn(&s->worker, mel_wasapi__worker, s, .name = "mel-audio-dev"))
    {
        mel_log_error("audio.wasapi", "failed to spawn device worker thread");
        CloseHandle(s->stop_event);
        s->stop_event = NULL;
        return;
    }
    s->worker_spawned = 1u;
}

void mel_audio_backend_stop(void)
{
    Mel_Wasapi* s = &mel_wasapi__state;

    if (s->worker_spawned)
    {
        if (s->stop_event != NULL)
            SetEvent(s->stop_event);
        mel_thread_join(&s->worker, NULL);
        s->worker_spawned = 0u;
    }

    if (s->stop_event != NULL)
    {
        CloseHandle(s->stop_event);
        s->stop_event = NULL;
    }

    u32 ur = atomic_load_explicit(&s->underruns, memory_order_relaxed);
    if (ur > 0u)
        mel_log_warn("audio.wasapi", "device stream stopped after %u underrun blocks", ur);
}

void mel_audio_backend_close(const Mel_Alloc* a)
{
    assert(a != NULL);
    Mel_Wasapi* s = &mel_wasapi__state;

    if (s->render != NULL)
    {
        IAudioRenderClient_Release(s->render);
        s->render = NULL;
    }
    if (s->client != NULL)
    {
        IAudioClient_Release(s->client);
        s->client = NULL;
    }
    if (s->device != NULL)
    {
        IMMDevice_Release(s->device);
        s->device = NULL;
    }
    if (s->enumerator != NULL)
    {
        IMMDeviceEnumerator_Release(s->enumerator);
        s->enumerator = NULL;
    }
    if (s->audio_event != NULL)
    {
        CloseHandle(s->audio_event);
        s->audio_event = NULL;
    }
    if (s->format != NULL)
    {
        mel_dealloc(a, s->format);
        s->format = NULL;
    }
    if (atomic_load_explicit(&s->com_owned, memory_order_relaxed) != 0u)
    {
        CoUninitialize();
        atomic_store_explicit(&s->com_owned, 0u, memory_order_relaxed);
    }
}

void mel_audio_backend_set_device_event(Mel_Event* ev)
{
    MEL_UNUSED(ev);
    if (ev != NULL)
        mel_log_info("audio.wasapi", "device-event hook installed but unfired (IMMNotificationClient hotplug listener not wired)");
}
