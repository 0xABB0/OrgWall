#define COBJMACROS
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mfobjects.h>

#include <camera/provider.h>

#include <image/image.h>
#include <image/format.h>
#include <image/geometry.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>

#include <log/log.h>

#include <string.h>

typedef struct
{
    const mel_image_format* fmt;
    GUID                    subtype;
} Mf_Format_Map;

static const Mf_Format_Map* mf_format_map(usize* count)
{
    static const Mf_Format_Map map[] = {
        { &mel_image_nv12, { 0x3231564E, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } } },
    };
    *count = sizeof(map) / sizeof(map[0]);
    return map;
}

static bool mf_subtype_for(const mel_image_format* fmt, GUID* out)
{
    usize                n = 0;
    const Mf_Format_Map* map = mf_format_map(&n);
    for (usize i = 0; i < n; i++)
        if (map[i].fmt == fmt)
        {
            *out = map[i].subtype;
            return true;
        }
    return false;
}

static const mel_image_format* mf_format_for(REFGUID subtype)
{
    usize                n = 0;
    const Mf_Format_Map* map = mf_format_map(&n);
    for (usize i = 0; i < n; i++)
        if (IsEqualGUID(&map[i].subtype, subtype))
            return map[i].fmt;
    return NULL;
}

typedef struct
{
    u64              stable_id;
    str8             name;
    WCHAR*           symlink;
    u32              symlink_len;
    Mel_Camera_Modes modes;
} Device_Rec;

typedef struct
{
    u64                stable_id;
    IMFSourceReader*   reader;
    Mel_Camera_Sink    sink;
    const mel_image_format* fmt;
    i32                w, h;
    HANDLE             thread;
    volatile LONG      running;
    volatile LONG      stop_request;
} Session;

typedef struct
{
    bool             initialized;
    bool             mf_started;
    const Mel_Alloc* alloc;
    Mel_Array(Device_Rec) devices;
    Mel_Array(Session*) sessions;
} Mf_Provider;

static Mf_Provider g_mf;

static u64 mf_hash_wide(const WCHAR* s, u32 len)
{
    u64 h = 1469598103934665603ull;
    for (u32 i = 0; i < len; i++)
    {
        h ^= (u64)(u16)s[i];
        h *= 1099511628211ull;
    }
    return h;
}

static str8 mf_utf8_from_wide(const WCHAR* w, const Mel_Alloc* a)
{
    if (!w)
        return (str8){ 0 };
    int bytes = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0)
        return (str8){ 0 };
    u8* buf = (u8*)mel_alloc(a, (usize)bytes);
    if (!buf)
        return (str8){ 0 };
    WideCharToMultiByte(CP_UTF8, 0, w, -1, (LPSTR)buf, bytes, NULL, NULL);
    return (str8){ buf, (size)(bytes - 1) };
}

static WCHAR* mf_dup_wide(const WCHAR* w, u32 len, const Mel_Alloc* a)
{
    WCHAR* out = (WCHAR*)mel_alloc(a, ((usize)len + 1) * sizeof(WCHAR));
    if (!out)
        return NULL;
    memcpy(out, w, (usize)len * sizeof(WCHAR));
    out[len] = 0;
    return out;
}

static void device_rec_free(Device_Rec* d)
{
    if (d->name.data)
        mel_dealloc(g_mf.alloc, d->name.data);
    if (d->symlink)
        mel_dealloc(g_mf.alloc, d->symlink);
    mel_array_free(&d->modes);
    memset(d, 0, sizeof *d);
}

static void devices_clear(void)
{
    for (usize i = 0; i < g_mf.devices.count; i++)
        device_rec_free(&g_mf.devices.items[i]);
    mel_array_clear(&g_mf.devices);
}

static void modes_collect(IMFMediaSource* source, Mel_Camera_Modes* modes)
{
    IMFSourceReader* reader = NULL;
    if (FAILED(MFCreateSourceReaderFromMediaSource(source, NULL, &reader)) || reader == NULL)
        return;

    for (DWORD mi = 0;; mi++)
    {
        IMFMediaType* mt = NULL;
        HRESULT       hr = IMFSourceReader_GetNativeMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, mi, &mt);
        if (hr == MF_E_NO_MORE_TYPES || FAILED(hr) || mt == NULL)
        {
            if (mt)
                IMFMediaType_Release(mt);
            break;
        }

        GUID subtype;
        if (SUCCEEDED(IMFMediaType_GetGUID(mt, &MF_MT_SUBTYPE, &subtype)))
        {
            const mel_image_format* fmt = mf_format_for(&subtype);
            if (fmt)
            {
                UINT32 w = 0, h = 0;
                MFGetAttributeSize((IMFAttributes*)mt, &MF_MT_FRAME_SIZE, &w, &h);
                UINT32 num = 0, den = 0;
                MFGetAttributeRatio((IMFAttributes*)mt, &MF_MT_FRAME_RATE, &num, &den);
                f32 fps = den != 0 ? (f32)num / (f32)den : 0.0f;
                Mel_Camera_Mode mode = {
                    .format = fmt,
                    .width = (i32)w,
                    .height = (i32)h,
                    .fps_min = fps,
                    .fps_max = fps,
                };
                mel_array_push(modes, mode);
            }
        }
        IMFMediaType_Release(mt);
    }

    IMFSourceReader_Release(reader);
}

static void devices_rebuild(void)
{
    devices_clear();

    IMFAttributes* attrs = NULL;
    if (FAILED(MFCreateAttributes(&attrs, 1)) || attrs == NULL)
    {
        mel_log_error("camera", "mf enumerate: MFCreateAttributes failed");
        return;
    }
    IMFAttributes_SetGUID(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** activs = NULL;
    UINT32        count = 0;
    HRESULT       hr = MFEnumDeviceSources(attrs, &activs, &count);
    IMFAttributes_Release(attrs);
    if (FAILED(hr))
    {
        mel_log_error("camera", "mf enumerate: MFEnumDeviceSources failed (0x%08lx)", (unsigned long)hr);
        return;
    }

    for (UINT32 i = 0; i < count; i++)
    {
        IMFActivate* act = activs[i];
        if (!act)
            continue;

        WCHAR* symlink = NULL;
        UINT32 symlen = 0;
        if (FAILED(IMFActivate_GetAllocatedString(act, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symlink, &symlen)) || symlink == NULL)
        {
            IMFActivate_Release(act);
            continue;
        }

        WCHAR* friendly = NULL;
        UINT32 frilen = 0;
        IMFActivate_GetAllocatedString(act, &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendly, &frilen);

        Device_Rec rec;
        memset(&rec, 0, sizeof rec);
        rec.stable_id = mf_hash_wide(symlink, symlen);
        rec.symlink = mf_dup_wide(symlink, symlen, g_mf.alloc);
        rec.symlink_len = symlen;
        rec.name = mf_utf8_from_wide(friendly ? friendly : symlink, g_mf.alloc);
        mel_array_init(&rec.modes, g_mf.alloc);

        IMFMediaSource* source = NULL;
        if (SUCCEEDED(IMFActivate_ActivateObject(act, &IID_IMFMediaSource, (void**)&source)) && source != NULL)
        {
            modes_collect(source, &rec.modes);
            IMFMediaSource_Shutdown(source);
            IMFMediaSource_Release(source);
        }

        mel_array_push(&g_mf.devices, rec);

        if (friendly)
            CoTaskMemFree(friendly);
        CoTaskMemFree(symlink);
        IMFActivate_Release(act);
    }

    CoTaskMemFree(activs);
}

static Device_Rec* device_find(u64 stable_id)
{
    for (usize i = 0; i < g_mf.devices.count; i++)
        if (g_mf.devices.items[i].stable_id == stable_id)
            return &g_mf.devices.items[i];
    return NULL;
}

static Session* session_find(u64 stable_id)
{
    for (usize i = 0; i < g_mf.sessions.count; i++)
        if (g_mf.sessions.items[i]->stable_id == stable_id)
            return g_mf.sessions.items[i];
    return NULL;
}

static u32 mf_enumerate(void* user, Mel_Camera_Raw* out, u32 cap)
{
    (void)user;
    devices_rebuild();
    u32 n = (u32)g_mf.devices.count < cap ? (u32)g_mf.devices.count : cap;
    for (u32 i = 0; i < n; i++)
    {
        Device_Rec* d = &g_mf.devices.items[i];
        out[i].stable_id = d->stable_id;
        out[i].name = d->name;
        out[i].facing = &mel_camera_external;
        out[i].modes = d->modes.items;
        out[i].mode_count = (u32)d->modes.count;
    }
    return n;
}

static const mel_camera_auth* mf_authorization(void* user)
{
    (void)user;
    return &mel_camera_auth_granted;
}

static void mf_authorize(void* user, Mel_Camera_Sink sink)
{
    (void)user;
    if (sink.on_auth)
        sink.on_auth(sink.token, &mel_camera_auth_granted);
}

static bool sample_deliver(Session* s, IMFSample* sample)
{
    IMFMediaBuffer* buffer = NULL;
    if (FAILED(IMFSample_ConvertToContiguousBuffer(sample, &buffer)) || buffer == NULL)
        return false;

    bool delivered = false;

    LONGLONG ts100ns = 0;
    IMFSample_GetSampleTime(sample, &ts100ns);
    u64 ns = (u64)ts100ns * 100ull;

    IMF2DBuffer2* b2 = NULL;
    IMF2DBuffer*  b1 = NULL;

    if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer2, (void**)&b2)) && b2 != NULL)
    {
        BYTE*  scanline0 = NULL;
        LONG   pitch = 0;
        BYTE*  start = NULL;
        DWORD  length = 0;
        if (SUCCEEDED(IMF2DBuffer2_Lock2DSize(b2, MF2DBuffer_LockFlags_Read, &scanline0, &pitch, &start, &length)))
        {
            i32 stride = (i32)(pitch < 0 ? -pitch : pitch);
            Mel_Image_Plane planes[2];
            planes[0] = (Mel_Image_Plane){ .pixels = (u8*)scanline0, .stride = stride, .w = s->w, .h = s->h, .bpp = 1 };
            planes[1] = (Mel_Image_Plane){ .pixels = (u8*)scanline0 + (usize)stride * (usize)s->h, .stride = stride, .w = s->w / 2, .h = s->h / 2, .bpp = 2 };

            Mel_Image image;
            if (mel_image_wrap(&image, s->fmt, s->w, s->h, planes, 2))
            {
                Mel_Camera_Frame frame = {
                    .image = image,
                    .timestamp_ns = ns,
                    .sequence = 0,
                    .orient = { .quarter_turns = 0, .flip_x = false },
                };
                if (s->sink.on_frame)
                    s->sink.on_frame(s->sink.token, &frame);
                delivered = true;
            }
            IMF2DBuffer2_Unlock2D(b2);
        }
        IMF2DBuffer2_Release(b2);
    }
    else if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer, (void**)&b1)) && b1 != NULL)
    {
        BYTE* scanline0 = NULL;
        LONG  pitch = 0;
        if (SUCCEEDED(IMF2DBuffer_Lock2D(b1, &scanline0, &pitch)))
        {
            i32 stride = (i32)(pitch < 0 ? -pitch : pitch);
            Mel_Image_Plane planes[2];
            planes[0] = (Mel_Image_Plane){ .pixels = (u8*)scanline0, .stride = stride, .w = s->w, .h = s->h, .bpp = 1 };
            planes[1] = (Mel_Image_Plane){ .pixels = (u8*)scanline0 + (usize)stride * (usize)s->h, .stride = stride, .w = s->w / 2, .h = s->h / 2, .bpp = 2 };

            Mel_Image image;
            if (mel_image_wrap(&image, s->fmt, s->w, s->h, planes, 2))
            {
                Mel_Camera_Frame frame = {
                    .image = image,
                    .timestamp_ns = ns,
                    .sequence = 0,
                    .orient = { .quarter_turns = 0, .flip_x = false },
                };
                if (s->sink.on_frame)
                    s->sink.on_frame(s->sink.token, &frame);
                delivered = true;
            }
            IMF2DBuffer_Unlock2D(b1);
        }
        IMF2DBuffer_Release(b1);
    }
    else
    {
        BYTE*  data = NULL;
        DWORD  maxlen = 0, curlen = 0;
        if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, &maxlen, &curlen)))
        {
            i32 stride = s->w;
            Mel_Image_Plane planes[2];
            planes[0] = (Mel_Image_Plane){ .pixels = (u8*)data, .stride = stride, .w = s->w, .h = s->h, .bpp = 1 };
            planes[1] = (Mel_Image_Plane){ .pixels = (u8*)data + (usize)stride * (usize)s->h, .stride = stride, .w = s->w / 2, .h = s->h / 2, .bpp = 2 };

            Mel_Image image;
            if (mel_image_wrap(&image, s->fmt, s->w, s->h, planes, 2))
            {
                Mel_Camera_Frame frame = {
                    .image = image,
                    .timestamp_ns = ns,
                    .sequence = 0,
                    .orient = { .quarter_turns = 0, .flip_x = false },
                };
                if (s->sink.on_frame)
                    s->sink.on_frame(s->sink.token, &frame);
                delivered = true;
            }
            IMFMediaBuffer_Unlock(buffer);
        }
    }

    IMFMediaBuffer_Release(buffer);
    return delivered;
}

static DWORD WINAPI capture_thread(LPVOID param)
{
    Session* s = (Session*)param;
    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED)))
    {
        mel_log_error("camera", "mf capture: CoInitializeEx failed");
        return 1;
    }

    while (InterlockedCompareExchange(&s->stop_request, 0, 0) == 0)
    {
        DWORD      stream_index = 0, flags = 0;
        LONGLONG   timestamp = 0;
        IMFSample* sample = NULL;
        HRESULT    hr = IMFSourceReader_ReadSample(s->reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream_index, &flags, &timestamp, &sample);
        if (FAILED(hr))
        {
            mel_log_error("camera", "mf capture: ReadSample failed (0x%08lx)", (unsigned long)hr);
            if (sample)
                IMFSample_Release(sample);
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            if (sample)
                IMFSample_Release(sample);
            break;
        }
        if (sample)
        {
            sample_deliver(s, sample);
            IMFSample_Release(sample);
        }
    }

    CoUninitialize();
    return 0;
}

static IMFMediaSource* source_from_symlink(const WCHAR* symlink, u32 symlen)
{
    IMFAttributes* attrs = NULL;
    if (FAILED(MFCreateAttributes(&attrs, 2)) || attrs == NULL)
        return NULL;
    IMFAttributes_SetGUID(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFAttributes_SetString(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, symlink);
    (void)symlen;

    IMFMediaSource* source = NULL;
    HRESULT         hr = MFCreateDeviceSource(attrs, &source);
    IMFAttributes_Release(attrs);
    if (FAILED(hr))
        return NULL;
    return source;
}

static bool reader_select_format(IMFSourceReader* reader, const GUID* subtype, i32 w, i32 h, f32 fps)
{
    for (DWORD mi = 0;; mi++)
    {
        IMFMediaType* mt = NULL;
        HRESULT       hr = IMFSourceReader_GetNativeMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, mi, &mt);
        if (hr == MF_E_NO_MORE_TYPES || FAILED(hr) || mt == NULL)
        {
            if (mt)
                IMFMediaType_Release(mt);
            break;
        }

        GUID   got;
        UINT32 mw = 0, mh = 0;
        bool   match = false;
        if (SUCCEEDED(IMFMediaType_GetGUID(mt, &MF_MT_SUBTYPE, &got)) && IsEqualGUID(&got, subtype))
        {
            MFGetAttributeSize((IMFAttributes*)mt, &MF_MT_FRAME_SIZE, &mw, &mh);
            if ((i32)mw == w && (i32)mh == h)
                match = true;
        }
        if (match)
        {
            if (fps > 0.0f)
                MFSetAttributeRatio((IMFAttributes*)mt, &MF_MT_FRAME_RATE, (UINT32)(fps + 0.5f), 1);
            HRESULT sr = IMFSourceReader_SetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, mt);
            IMFMediaType_Release(mt);
            return SUCCEEDED(sr);
        }
        IMFMediaType_Release(mt);
    }
    return false;
}

static bool mf_open(void* user, u64 stable_id, Mel_Camera_Config cfg, Mel_Camera_Sink sink)
{
    (void)user;
    if (session_find(stable_id))
    {
        mel_log_error("camera", "mf open: device %llu already open", (unsigned long long)stable_id);
        return false;
    }
    Device_Rec* dev = device_find(stable_id);
    if (!dev || !dev->symlink)
    {
        mel_log_error("camera", "mf open: device %llu not found", (unsigned long long)stable_id);
        return false;
    }

    GUID subtype;
    if (!mf_subtype_for(cfg.format, &subtype))
    {
        mel_log_error("camera", "mf open: pixel format unsupported by backend");
        return false;
    }

    IMFMediaSource* source = source_from_symlink(dev->symlink, dev->symlink_len);
    if (!source)
    {
        mel_log_error("camera", "mf open: MFCreateDeviceSource failed for %llu", (unsigned long long)stable_id);
        return false;
    }

    IMFSourceReader* reader = NULL;
    HRESULT          hr = MFCreateSourceReaderFromMediaSource(source, NULL, &reader);
    if (FAILED(hr) || reader == NULL)
    {
        mel_log_error("camera", "mf open: MFCreateSourceReaderFromMediaSource failed (0x%08lx)", (unsigned long)hr);
        IMFMediaSource_Shutdown(source);
        IMFMediaSource_Release(source);
        return false;
    }

    if (!reader_select_format(reader, &subtype, cfg.width, cfg.height, cfg.fps))
    {
        mel_log_error("camera", "mf open: no native media type for %dx%d", cfg.width, cfg.height);
        IMFSourceReader_Release(reader);
        IMFMediaSource_Shutdown(source);
        IMFMediaSource_Release(source);
        return false;
    }

    IMFMediaSource_Release(source);

    Session* s = mel_alloc_type(g_mf.alloc, Session);
    if (!s)
    {
        IMFSourceReader_Release(reader);
        return false;
    }
    memset(s, 0, sizeof *s);
    s->stable_id = stable_id;
    s->reader = reader;
    s->sink = sink;
    s->fmt = cfg.format;
    s->w = cfg.width;
    s->h = cfg.height;
    s->running = 0;
    s->stop_request = 0;

    mel_array_push(&g_mf.sessions, s);
    return true;
}

static void session_stop_thread(Session* s)
{
    if (s->thread)
    {
        InterlockedExchange(&s->stop_request, 1);
        WaitForSingleObject(s->thread, INFINITE);
        CloseHandle(s->thread);
        s->thread = NULL;
    }
    InterlockedExchange(&s->running, 0);
    InterlockedExchange(&s->stop_request, 0);
}

static void mf_close(void* user, u64 stable_id)
{
    (void)user;
    for (usize i = 0; i < g_mf.sessions.count; i++)
    {
        Session* s = g_mf.sessions.items[i];
        if (s->stable_id != stable_id)
            continue;
        session_stop_thread(s);
        if (s->reader)
            IMFSourceReader_Release(s->reader);
        mel_dealloc(g_mf.alloc, s);
        g_mf.sessions.items[i] = g_mf.sessions.items[g_mf.sessions.count - 1];
        g_mf.sessions.count--;
        return;
    }
}

static Mel_Camera_Status mf_start(void* user, u64 stable_id)
{
    (void)user;
    Session* s = session_find(stable_id);
    if (!s)
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
    if (InterlockedCompareExchange(&s->running, 0, 0) != 0)
        return MEL_CAMERA_OK;
    InterlockedExchange(&s->stop_request, 0);
    s->thread = CreateThread(NULL, 0, capture_thread, s, 0, NULL);
    if (!s->thread)
    {
        mel_log_error("camera", "mf start: CreateThread failed");
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_BUSY;
    }
    InterlockedExchange(&s->running, 1);
    return MEL_CAMERA_OK;
}

static Mel_Camera_Status mf_stop(void* user, u64 stable_id)
{
    (void)user;
    Session* s = session_find(stable_id);
    if (!s)
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
    session_stop_thread(s);
    return MEL_CAMERA_OK;
}

static void* mf_native(void* user, u64 stable_id)
{
    (void)user;
    Session* s = session_find(stable_id);
    return s ? (void*)s->reader : NULL;
}

static Mel_Camera_Provider_Desc g_desc;

void mel_camera__register_host_providers(void)
{
    if (!g_mf.initialized)
    {
        g_mf.alloc = mel_alloc_heap();
        mel_array_init(&g_mf.devices, g_mf.alloc);
        mel_array_init(&g_mf.sessions, g_mf.alloc);
        HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        g_mf.mf_started = SUCCEEDED(hr);
        if (!g_mf.mf_started)
            mel_log_error("camera", "mf register: MFStartup failed (0x%08lx)", (unsigned long)hr);
        g_mf.initialized = true;
    }

    g_desc = (Mel_Camera_Provider_Desc){
        .name = "win32-media-foundation",
        .enumerate = mf_enumerate,
        .open = mf_open,
        .close = mf_close,
        .start = mf_start,
        .stop = mf_stop,
        .authorization = mf_authorization,
        .authorize = mf_authorize,
        .native = mf_native,
    };
    mel_camera_provider_register(&g_desc);
}
