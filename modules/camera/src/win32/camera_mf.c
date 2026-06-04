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

static bool mf_attr_size(IMFMediaType* mt, REFGUID key, UINT32* hi, UINT32* lo)
{
    UINT64 packed = 0;
    if (FAILED(IMFAttributes_GetUINT64((IMFAttributes*)mt, key, &packed)))
        return false;
    *hi = (UINT32)(packed >> 32);
    *lo = (UINT32)(packed & 0xFFFFFFFFull);
    return true;
}

static void mf_attr_set_size(IMFMediaType* mt, REFGUID key, UINT32 hi, UINT32 lo)
{
    UINT64 packed = ((UINT64)hi << 32) | (UINT64)lo;
    IMFAttributes_SetUINT64((IMFAttributes*)mt, key, packed);
}

static i32 mf_default_stride(IMFMediaType* mt, i32 w)
{
    UINT32 ds = 0;
    if (SUCCEEDED(IMFAttributes_GetUINT32((IMFAttributes*)mt, &MF_MT_DEFAULT_STRIDE, &ds)) && ds != 0)
    {
        LONG s = (LONG)ds;
        return (i32)(s < 0 ? -s : s);
    }
    GUID subtype;
    if (SUCCEEDED(IMFMediaType_GetGUID(mt, &MF_MT_SUBTYPE, &subtype)))
    {
        LONG s = 0;
        if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(subtype.Data1, (UINT32)w, &s)) && s != 0)
            return (i32)(s < 0 ? -s : s);
    }
    return 0;
}

typedef struct
{
    u64    stable_id;
    str8   name;
    WCHAR* symlink;
} Device_Rec;

typedef bool (*Mf_Lock_Fn)(IMFMediaBuffer* buffer, struct mf_session* s, IMFSample* sample, u64 ns);

typedef struct mf_session
{
    u64                     stable_id;
    IMFSourceReader*        reader;
    Mel_Camera_Sink         sink;
    const mel_image_format* fmt;
    i32                     w, h;
    i32                     default_stride;
    Mf_Lock_Fn              lock_fn;
    HANDLE                  thread;
    volatile LONG           running;
    volatile LONG           stop_request;
    volatile LONG           warned_multibuffer;
    volatile LONG           warned_wrapfail;
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
    memset(d, 0, sizeof *d);
}

static void devices_clear(void)
{
    for (usize i = 0; i < g_mf.devices.count; i++)
        device_rec_free(&g_mf.devices.items[i]);
    mel_array_clear(&g_mf.devices);
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
        rec.name = mf_utf8_from_wide(friendly ? friendly : symlink, g_mf.alloc);

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
    if (!g_mf.mf_started)
    {
        mel_log_error("camera", "mf enumerate: Media Foundation not started");
        return 0;
    }
    devices_rebuild();
    u32 n = (u32)g_mf.devices.count < cap ? (u32)g_mf.devices.count : cap;
    for (u32 i = 0; i < n; i++)
    {
        Device_Rec* d = &g_mf.devices.items[i];
        out[i].stable_id = d->stable_id;
        out[i].name = d->name;
        out[i].facing = &mel_camera_external;
        out[i].modes = NULL;
        out[i].mode_count = 0;
    }
    return n;
}

static const mel_camera_auth* mf_consent(void)
{
    WCHAR   data[32];
    DWORD   size = sizeof data;
    DWORD   type = 0;
    LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam", L"Value", RRF_RT_REG_SZ, &type, data, &size);
    if (st != ERROR_SUCCESS)
        return &mel_camera_auth_not_determined;
    if (lstrcmpiW(data, L"Allow") == 0)
        return &mel_camera_auth_granted;
    if (lstrcmpiW(data, L"Deny") == 0)
        return &mel_camera_auth_denied;
    return &mel_camera_auth_not_determined;
}

static const mel_camera_auth* mf_authorization(void* user)
{
    (void)user;
    return mf_consent();
}

static void mf_authorize(void* user, Mel_Camera_Sink sink)
{
    (void)user;
    if (sink.on_auth)
        sink.on_auth(sink.token, mf_consent());
}

static bool deliver_nv12(Session* s, BYTE* base, i32 stride, u64 ns)
{
    Mel_Image_Plane planes[2];
    planes[0] = (Mel_Image_Plane){ .pixels = (u8*)base, .stride = stride, .w = s->w, .h = s->h, .bpp = 1 };
    planes[1] = (Mel_Image_Plane){ .pixels = (u8*)base + (usize)stride * (usize)s->h, .stride = stride, .w = s->w / 2, .h = s->h / 2, .bpp = 2 };

    Mel_Image image;
    if (!mel_image_wrap(&image, s->fmt, s->w, s->h, planes, 2))
    {
        if (InterlockedExchange(&s->warned_wrapfail, 1) == 0)
            mel_log_error("camera", "mf capture: mel_image_wrap rejected buffer (stride=%d %dx%d) - stream will be black", stride, s->w, s->h);
        return false;
    }
    Mel_Camera_Frame frame = {
        .image = image,
        .timestamp_ns = ns,
        .sequence = 0,
        .orient = { .quarter_turns = 0, .flip_x = false },
    };
    if (s->sink.on_frame)
        s->sink.on_frame(s->sink.token, &frame);
    return true;
}

static bool mf_pitch_ok(Session* s, LONG pitch)
{
    if (pitch >= 0)
        return true;
    if (InterlockedExchange(&s->warned_wrapfail, 1) == 0)
        mel_log_error("camera", "mf capture: bottom-up buffer (pitch=%ld) unsupported; rejecting frame to avoid OOB read", pitch);
    return false;
}

static bool lock_b2(IMFMediaBuffer* buffer, Session* s, IMFSample* sample, u64 ns)
{
    (void)sample;
    IMF2DBuffer2* b2 = NULL;
    if (FAILED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer2, (void**)&b2)) || b2 == NULL)
        return false;
    bool  delivered = false;
    BYTE* scanline0 = NULL;
    LONG  pitch = 0;
    BYTE* start = NULL;
    DWORD length = 0;
    if (SUCCEEDED(IMF2DBuffer2_Lock2DSize(b2, MF2DBuffer_LockFlags_Read, &scanline0, &pitch, &start, &length)))
    {
        (void)start;
        (void)length;
        if (mf_pitch_ok(s, pitch))
            delivered = deliver_nv12(s, scanline0, (i32)pitch, ns);
        IMF2DBuffer2_Unlock2D(b2);
    }
    IMF2DBuffer2_Release(b2);
    return delivered;
}

static bool lock_b1(IMFMediaBuffer* buffer, Session* s, IMFSample* sample, u64 ns)
{
    (void)sample;
    IMF2DBuffer* b1 = NULL;
    if (FAILED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer, (void**)&b1)) || b1 == NULL)
        return false;
    bool  delivered = false;
    BYTE* scanline0 = NULL;
    LONG  pitch = 0;
    if (SUCCEEDED(IMF2DBuffer_Lock2D(b1, &scanline0, &pitch)))
    {
        if (mf_pitch_ok(s, pitch))
            delivered = deliver_nv12(s, scanline0, (i32)pitch, ns);
        IMF2DBuffer_Unlock2D(b1);
    }
    IMF2DBuffer_Release(b1);
    return delivered;
}

static bool lock_plain(IMFMediaBuffer* buffer, Session* s, IMFSample* sample, u64 ns)
{
    (void)sample;
    if (s->default_stride <= 0)
    {
        if (InterlockedExchange(&s->warned_wrapfail, 1) == 0)
            mel_log_error("camera", "mf capture: plain Lock path has no known stride; rejecting frame");
        return false;
    }
    BYTE* data = NULL;
    DWORD maxlen = 0, curlen = 0;
    bool  delivered = false;
    if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, &maxlen, &curlen)))
    {
        delivered = deliver_nv12(s, data, s->default_stride, ns);
        IMFMediaBuffer_Unlock(buffer);
    }
    return delivered;
}

static Mf_Lock_Fn mf_resolve_lock_fn(IMFMediaBuffer* buffer)
{
    IMF2DBuffer2* b2 = NULL;
    if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer2, (void**)&b2)) && b2 != NULL)
    {
        IMF2DBuffer2_Release(b2);
        return lock_b2;
    }
    IMF2DBuffer* b1 = NULL;
    if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer, (void**)&b1)) && b1 != NULL)
    {
        IMF2DBuffer_Release(b1);
        return lock_b1;
    }
    return lock_plain;
}

static IMFMediaBuffer* sample_single_buffer(Session* s, IMFSample* sample)
{
    DWORD bufcount = 0;
    if (FAILED(IMFSample_GetBufferCount(sample, &bufcount)))
        return NULL;
    if (bufcount == 1)
    {
        IMFMediaBuffer* buffer = NULL;
        if (FAILED(IMFSample_GetBufferByIndex(sample, 0, &buffer)) || buffer == NULL)
            return NULL;
        return buffer;
    }
    if (InterlockedExchange(&s->warned_multibuffer, 1) == 0)
        mel_log_warn("camera", "mf capture: multi-buffer sample (%lu); falling back to ConvertToContiguousBuffer (per-frame copy)", (unsigned long)bufcount);
    IMFMediaBuffer* buffer = NULL;
    if (FAILED(IMFSample_ConvertToContiguousBuffer(sample, &buffer)) || buffer == NULL)
        return NULL;
    return buffer;
}

static bool session_resync_type(Session* s)
{
    IMFMediaType* mt = NULL;
    if (FAILED(IMFSourceReader_GetCurrentMediaType(s->reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &mt)) || mt == NULL)
    {
        mel_log_error("camera", "mf capture: media type changed but GetCurrentMediaType failed");
        return false;
    }
    GUID subtype;
    GUID want;
    bool ok = false;
    if (SUCCEEDED(IMFMediaType_GetGUID(mt, &MF_MT_SUBTYPE, &subtype)) && mf_subtype_for(s->fmt, &want) && IsEqualGUID(&subtype, &want))
    {
        UINT32 w = 0, h = 0;
        if (mf_attr_size(mt, &MF_MT_FRAME_SIZE, &w, &h) && w > 0 && h > 0)
        {
            s->w = (i32)w;
            s->h = (i32)h;
            s->default_stride = mf_default_stride(mt, s->w);
            s->lock_fn = NULL;
            ok = true;
        }
    }
    if (!ok)
        mel_log_error("camera", "mf capture: media type changed to an unsupported format; dropping frames");
    IMFMediaType_Release(mt);
    return ok;
}

static bool sample_deliver(Session* s, IMFSample* sample)
{
    IMFMediaBuffer* buffer = sample_single_buffer(s, sample);
    if (!buffer)
        return false;

    LONGLONG ts100ns = 0;
    IMFSample_GetSampleTime(sample, &ts100ns);
    u64 ns = (u64)ts100ns * 100ull;

    if (!s->lock_fn)
        s->lock_fn = mf_resolve_lock_fn(buffer);

    bool delivered = s->lock_fn(buffer, s, sample, ns);

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
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
        {
            if (!session_resync_type(s))
            {
                if (sample)
                    IMFSample_Release(sample);
                continue;
            }
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

static IMFMediaSource* source_from_symlink(const WCHAR* symlink)
{
    IMFAttributes* attrs = NULL;
    if (FAILED(MFCreateAttributes(&attrs, 2)) || attrs == NULL)
        return NULL;
    IMFAttributes_SetGUID(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFAttributes_SetString(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, symlink);

    IMFMediaSource* source = NULL;
    HRESULT         hr = MFCreateDeviceSource(attrs, &source);
    IMFAttributes_Release(attrs);
    if (FAILED(hr))
        return NULL;
    return source;
}

static bool mf_fps_in_range(IMFMediaType* mt, f32 fps)
{
    UINT32 lo_n = 0, lo_d = 0, hi_n = 0, hi_d = 0;
    bool   have_lo = mf_attr_size(mt, &MF_MT_FRAME_RATE_RANGE_MIN, &lo_n, &lo_d);
    bool   have_hi = mf_attr_size(mt, &MF_MT_FRAME_RATE_RANGE_MAX, &hi_n, &hi_d);
    if (!have_lo || !have_hi || lo_d == 0 || hi_d == 0)
    {
        UINT32 n = 0, d = 0;
        if (!mf_attr_size(mt, &MF_MT_FRAME_RATE, &n, &d) || d == 0)
            return false;
        f32 r = (f32)n / (f32)d;
        return fps <= r + 0.5f && fps >= r - 0.5f;
    }
    f32 lo = (f32)lo_n / (f32)lo_d;
    f32 hi = (f32)hi_n / (f32)hi_d;
    return fps >= lo - 0.5f && fps <= hi + 0.5f;
}

static bool reader_select_format(IMFSourceReader* reader, const GUID* subtype, i32 w, i32 h, f32 fps, i32* out_stride)
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
            mf_attr_size(mt, &MF_MT_FRAME_SIZE, &mw, &mh);
            if ((i32)mw == w && (i32)mh == h)
                match = true;
        }
        if (match)
        {
            if (fps > 0.0f)
            {
                if (mf_fps_in_range(mt, fps))
                    mf_attr_set_size(mt, &MF_MT_FRAME_RATE, (UINT32)(fps + 0.5f), 1);
                else
                    mel_log_warn("camera", "mf open: requested %.1f fps outside native range; using device default", (double)fps);
            }
            HRESULT sr = IMFSourceReader_SetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, mt);
            if (SUCCEEDED(sr))
            {
                IMFMediaType* cur = NULL;
                if (SUCCEEDED(IMFSourceReader_GetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur)) && cur != NULL)
                {
                    *out_stride = mf_default_stride(cur, w);
                    IMFMediaType_Release(cur);
                }
                else
                    *out_stride = mf_default_stride(mt, w);
            }
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
    if (!g_mf.mf_started)
    {
        mel_log_error("camera", "mf open: Media Foundation not started");
        return false;
    }
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

    IMFMediaSource* source = source_from_symlink(dev->symlink);
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

    i32 stride = 0;
    if (!reader_select_format(reader, &subtype, cfg.width, cfg.height, cfg.fps, &stride))
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
    s->default_stride = stride;
    s->lock_fn = NULL;
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
    if (InterlockedCompareExchange(&s->running, 1, 0) != 0)
        return MEL_CAMERA_OK;
    InterlockedExchange(&s->stop_request, 0);
    s->thread = CreateThread(NULL, 0, capture_thread, s, 0, NULL);
    if (!s->thread)
    {
        InterlockedExchange(&s->running, 0);
        mel_log_error("camera", "mf start: CreateThread failed");
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_BUSY;
    }
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
