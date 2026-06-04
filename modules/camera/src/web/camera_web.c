#include <camera/provider.h>

#include <image/image.h>
#include <image/format.h>
#include <image/geometry.h>

#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "camera_web.c is for the emscripten runtime only"
#endif

#include <allocator/allocator.h>
#include <collection.array/array.h>
#include <debug/assert.h>
#include <log/log.h>

#include <emscripten.h>

#include <string.h>

typedef struct
{
    u64             stable_id;
    Mel_Camera_Sink sink;
    bool            have_sink;
    u8*             buf;
    usize           cap;
    i32             w, h;
} Web_Session;

typedef struct
{
    u64  stable_id;
    str8 name;
} Web_Device;

static struct
{
    const Mel_Alloc* alloc;
    Mel_Array(Web_Session) sessions;
    Mel_Array(Web_Device) devices;
    Mel_Camera_Sink        auth_sink;
    bool                   have_auth_sink;
    const mel_camera_auth* last_auth;
} g_web;

EM_JS(void, mel_camera_web__ensure_init, (void), {
    if (globalThis.MelCam)
        return;
    globalThis.MelCam = { ready : false, devices : [], open : {} };
    var refresh = function()
    {
        if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices)
        {
            MelCam.ready = true;
            return;
        }
        navigator.mediaDevices.enumerateDevices()
            .then(function(list) {
                MelCam.devices = list.filter(function(d) { return d.kind == 'videoinput'; });
                MelCam.ready = true;
                if (typeof _mel_camera_web__on_devicechange == 'function')
                    _mel_camera_web__on_devicechange();
            })
            .catch(function() { MelCam.ready = true; });
    };
    MelCam.refresh = refresh;
    refresh();
    if (navigator.mediaDevices)
        navigator.mediaDevices.addEventListener('devicechange', refresh);
});

EM_JS(int, mel_camera_web__ready, (void), { return (globalThis.MelCam && MelCam.ready) ? 1 : 0; });

EM_JS(int, mel_camera_web__count, (void), { return (globalThis.MelCam && MelCam.ready) ? MelCam.devices.length : 0; });

EM_JS(unsigned, mel_camera_web__device_hash, (int idx), {
    var d = globalThis.MelCam && MelCam.devices[idx];
    if (!d)
        return 0;
    var s = (d.deviceId || ('cam' + idx));
    var h = 2166136261 >>> 0;
    for (var i = 0; i < s.length; i++)
    {
        h ^= s.charCodeAt(i);
        h = Math.imul(h, 16777619) >>> 0;
    }
    return h >>> 0;
});

EM_JS(void, mel_camera_web__device_name, (int idx, char* buf, int cap), {
    var d = globalThis.MelCam && MelCam.devices[idx];
    var name = (d && d.label) ? d.label : ('Camera ' + idx);
    stringToUTF8(name, buf, cap);
});

EM_JS(int, mel_camera_web__device_id_for_hash, (unsigned id_lo, unsigned id_hi, char* buf, int cap), {
    if (!globalThis.MelCam)
        return 0;
    var want = id_hi * 4294967296 + id_lo;
    for (var idx = 0; idx < MelCam.devices.length; idx++)
    {
        var d = MelCam.devices[idx];
        var s = (d.deviceId || ('cam' + idx));
        var h = 2166136261 >>> 0;
        for (var i = 0; i < s.length; i++)
        {
            h ^= s.charCodeAt(i);
            h = Math.imul(h, 16777619) >>> 0;
        }
        if ((h >>> 0) == want)
        {
            stringToUTF8(d.deviceId ? d.deviceId : "", buf, cap);
            return d.deviceId ? 2 : 1;
        }
    }
    return 0;
});

EM_JS(void, mel_camera_web__authorize, (void), {
    mel_camera_web__ensure_init();
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia)
    {
        _mel_camera_web__on_auth(0);
        return;
    }
    navigator.mediaDevices.getUserMedia({ video : true })
        .then(function(stream) {
            stream.getTracks().forEach(function(t) { t.stop(); });
            if (MelCam.refresh)
                MelCam.refresh();
            _mel_camera_web__on_auth(1);
        })
        .catch(function() { _mel_camera_web__on_auth(0); });
});

EM_JS(int, mel_camera_web__open, (unsigned id_lo, unsigned id_hi, const char* device_id, int len, int want_w, int want_h, double fps), {
    mel_camera_web__ensure_init();
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia)
        return 0;
    var key = id_hi * 4294967296 + id_lo;
    var did = len > 0 ? UTF8ToString(device_id, len) : "";
    var video = { width : { exact : want_w }, height : { exact : want_h } };
    if (fps > 0)
        video.frameRate = { exact : fps };
    if (did)
        video.deviceId = { exact : did };
    var st = {
        stream : null,
        video : null,
        canvas : null,
        ctx : null,
        raf : 0,
        running : false,
        id_lo : id_lo,
        id_hi : id_hi,
    };
    MelCam.open[key] = st;
    navigator.mediaDevices.getUserMedia({ video : video })
        .then(function(stream) {
            if (!MelCam.open[key])
            {
                stream.getTracks().forEach(function(t) { t.stop(); });
                return;
            }
            st.stream = stream;
            var v = document.createElement('video');
            v.muted = true;
            v.autoplay = true;
            v.playsInline = true;
            v.srcObject = stream;
            st.video = v;
            v.play().catch(function(){});
            _mel_camera_web__on_open_result(id_lo, id_hi, 1);
        })
        .catch(function(e) {
            delete MelCam.open[key];
            _mel_camera_web__on_open_result(id_lo, id_hi, 0);
        });
    return 1;
});

EM_JS(int, mel_camera_web__start, (unsigned id_lo, unsigned id_hi), {
    var key = id_hi * 4294967296 + id_lo;
    var st = globalThis.MelCam && MelCam.open[key];
    if (!st)
        return 0;
    if (st.running)
        return 1;
    st.running = true;
    var pump = function()
    {
        if (!st.running)
            return;
        st.raf = requestAnimationFrame(pump);
        var v = st.video;
        if (!v || v.readyState < 2 || v.videoWidth == 0)
            return;
        var w = v.videoWidth, h = v.videoHeight;
        if (!st.canvas || st.canvas.width != w || st.canvas.height != h)
        {
            st.canvas = (typeof OffscreenCanvas != 'undefined') ? new OffscreenCanvas(w, h) : document.createElement('canvas');
            st.canvas.width = w;
            st.canvas.height = h;
            st.ctx = st.canvas.getContext('2d', { willReadFrequently : true });
        }
        st.ctx.drawImage(v, 0, 0, w, h);
        var img;
        try { img = st.ctx.getImageData(0, 0, w, h); }
        catch(e) { return; }
        var bytes = w * h * 4;
        var ptr = _mel_camera_web__ensure_buf(st.id_lo, st.id_hi, w, h, bytes);
        if (!ptr)
            return;
        HEAPU8.set(img.data, ptr);
        _mel_camera_web__on_frame(st.id_lo, st.id_hi, ptr, w, h, performance.now());
    };
    st.raf = requestAnimationFrame(pump);
    return 1;
});

EM_JS(int, mel_camera_web__stop, (unsigned id_lo, unsigned id_hi), {
    var key = id_hi * 4294967296 + id_lo;
    var st = globalThis.MelCam && MelCam.open[key];
    if (!st)
        return 0;
    st.running = false;
    if (st.raf)
        cancelAnimationFrame(st.raf);
    st.raf = 0;
    return 1;
});

EM_JS(void, mel_camera_web__close, (unsigned id_lo, unsigned id_hi), {
    var key = id_hi * 4294967296 + id_lo;
    var st = globalThis.MelCam && MelCam.open[key];
    if (!st)
        return;
    st.running = false;
    if (st.raf)
        cancelAnimationFrame(st.raf);
    if (st.stream)
        st.stream.getTracks().forEach(function(t) { t.stop(); });
    if (st.video)
    {
        st.video.srcObject = null;
        st.video = null;
    }
    delete MelCam.open[key];
});

static Web_Session* web_session_find(u64 stable_id)
{
    for (usize i = 0; i < g_web.sessions.count; i++)
        if (g_web.sessions.items[i].stable_id == stable_id)
            return &g_web.sessions.items[i];
    return NULL;
}

static void web_session_reap(Web_Session* s)
{
    if (!s)
        return;
    s->have_sink = false;
    if (s->buf)
        mel_dealloc(g_web.alloc, s->buf);
    *s = g_web.sessions.items[g_web.sessions.count - 1];
    g_web.sessions.count--;
}

EMSCRIPTEN_KEEPALIVE void* mel_camera_web__ensure_buf(unsigned id_lo, unsigned id_hi, int w, int h, int bytes)
{
    u64          stable_id = ((u64)id_hi << 32) | (u64)id_lo;
    Web_Session* s = web_session_find(stable_id);
    if (!s || bytes <= 0)
        return NULL;
    if ((usize)bytes > s->cap)
    {
        u8* nb = (u8*)mel_realloc(g_web.alloc, s->buf, (usize)bytes);
        if (!nb)
        {
            mel_log_error("camera", "web: frame buffer realloc to %d bytes failed", bytes);
            return NULL;
        }
        s->buf = nb;
        s->cap = (usize)bytes;
    }
    s->w = w;
    s->h = h;
    return s->buf;
}

EMSCRIPTEN_KEEPALIVE void mel_camera_web__on_frame(unsigned id_lo, unsigned id_hi, void* ptr, int w, int h, double ts_ms)
{
    u64          stable_id = ((u64)id_hi << 32) | (u64)id_lo;
    Web_Session* s = web_session_find(stable_id);
    if (!s || !s->have_sink || s->sink.on_frame == NULL || ptr == NULL)
        return;

    Mel_Image_Plane plane = {
        .pixels = (u8*)ptr,
        .stride = w * 4,
        .w = w,
        .h = h,
        .bpp = 4,
    };
    Mel_Image image;
    if (!mel_image_wrap(&image, &mel_image_rgba8, w, h, &plane, 1))
        return;

    Mel_Camera_Frame frame = {
        .image = image,
        .timestamp_ns = (u64)(ts_ms * 1.0e6),
        .sequence = 0,
        .orient = { .quarter_turns = 0, .flip_x = false },
    };
    s->sink.on_frame(s->sink.token, &frame);
}

EMSCRIPTEN_KEEPALIVE void mel_camera_web__on_open_result(unsigned id_lo, unsigned id_hi, int ok)
{
    if (ok)
        return;
    u64 stable_id = ((u64)id_hi << 32) | (u64)id_lo;
    mel_log_error("camera", "web open: getUserMedia rejected for device %llu (permission denied, device busy, or unsatisfiable config); no frames will arrive", (unsigned long long)stable_id);
    web_session_reap(web_session_find(stable_id));
}

EMSCRIPTEN_KEEPALIVE void mel_camera_web__on_auth(int granted)
{
    g_web.last_auth = granted ? &mel_camera_auth_granted : &mel_camera_auth_denied;
    if (!g_web.have_auth_sink || g_web.auth_sink.on_auth == NULL)
        return;
    Mel_Camera_Sink sink = g_web.auth_sink;
    g_web.have_auth_sink = false;
    g_web.auth_sink = (Mel_Camera_Sink){ 0 };
    sink.on_auth(sink.token, g_web.last_auth);
}

EMSCRIPTEN_KEEPALIVE void mel_camera_web__on_devicechange(void) {}

static void web_devices_clear(void)
{
    for (usize i = 0; i < g_web.devices.count; i++)
        mel_dealloc(g_web.alloc, g_web.devices.items[i].name.data);
    mel_array_clear(&g_web.devices);
}

static str8 web_intern_name(const char* utf8)
{
    usize len = utf8 ? strlen(utf8) : 0;
    u8*   data = (u8*)mel_alloc(g_web.alloc, len + 1);
    if (!data)
        return (str8){ 0 };
    if (len)
        memcpy(data, utf8, len);
    data[len] = 0;
    return (str8){ data, (size)len };
}

static void web_ensure_arrays(const Mel_Alloc* alloc)
{
    mel_assert((g_web.sessions.allocator == NULL || g_web.sessions.allocator == alloc) && "web: allocator diverged across calls; per-element frees would cross pools");
    mel_assert((g_web.devices.allocator == NULL || g_web.devices.allocator == alloc) && "web: allocator diverged across calls; per-element frees would cross pools");
    g_web.alloc = alloc;
    if (g_web.sessions.allocator == NULL)
        mel_array_init(&g_web.sessions, alloc);
    if (g_web.devices.allocator == NULL)
        mel_array_init(&g_web.devices, alloc);
}

static u32 web_enumerate(void* user, const Mel_Alloc* alloc, Mel_Camera_Raw* out, u32 cap)
{
    (void)user;
    web_ensure_arrays(alloc);
    mel_camera_web__ensure_init();
    if (!mel_camera_web__ready())
        return 0;

    web_devices_clear();

    i32 count = (i32)mel_camera_web__count();
    u32 n = 0;
    for (i32 i = 0; i < count; i++)
    {
        char name_buf[256];
        mel_camera_web__device_name(i, name_buf, (int)sizeof name_buf);
        u64 stable_id = (u64)mel_camera_web__device_hash(i);

        Web_Device dev = { .stable_id = stable_id, .name = web_intern_name(name_buf) };
        mel_array_push(&g_web.devices, dev);

        if (n < cap && out)
        {
            out[n].stable_id = stable_id;
            out[n].name = dev.name;
            out[n].facing = &mel_camera_unknown;
            out[n].modes = NULL;
            out[n].mode_count = 0;
        }
        n++;
    }
    return n;
}

static const mel_camera_auth* web_authorization(void* user)
{
    (void)user;
    return g_web.last_auth ? g_web.last_auth : &mel_camera_auth_not_determined;
}

static void web_authorize(void* user, Mel_Camera_Sink sink)
{
    (void)user;
    if (sink.on_auth == NULL)
        return;
    g_web.auth_sink = sink;
    g_web.have_auth_sink = true;
    mel_camera_web__authorize();
}

static bool web_open(void* user, const Mel_Alloc* alloc, u64 stable_id, Mel_Camera_Config cfg, Mel_Camera_Sink sink)
{
    (void)user;
    web_ensure_arrays(alloc);
    if (cfg.format != &mel_image_rgba8)
    {
        mel_log_error("camera", "web open: only rgba8 is delivered by the canvas backend");
        return false;
    }
    if (web_session_find(stable_id))
    {
        mel_log_error("camera", "web open: device %llu already open", (unsigned long long)stable_id);
        return false;
    }

    char device_id[256];
    device_id[0] = 0;
    int resolved = mel_camera_web__device_id_for_hash((unsigned)(stable_id & 0xffffffffu), (unsigned)(stable_id >> 32), device_id, (int)sizeof device_id);
    if (resolved == 0)
    {
        mel_log_error("camera", "web open: no enumerated device hashes to stable_id %llu; call mel_camera_refresh after a hotplug", (unsigned long long)stable_id);
        return false;
    }
    if (resolved == 1)
    {
        mel_log_error("camera", "web open: device %llu has no deviceId yet (labels withheld until permission granted); call mel_camera_authorize first", (unsigned long long)stable_id);
        return false;
    }

    Web_Session s = {
        .stable_id = stable_id,
        .sink = sink,
        .have_sink = true,
        .buf = NULL,
        .cap = 0,
        .w = cfg.width,
        .h = cfg.height,
    };
    mel_array_push(&g_web.sessions, s);

    if (!mel_camera_web__open((unsigned)(stable_id & 0xffffffffu), (unsigned)(stable_id >> 32), device_id, (int)strlen(device_id), cfg.width, cfg.height, (double)cfg.fps))
    {
        mel_log_error("camera", "web open: getUserMedia unavailable");
        web_session_reap(web_session_find(stable_id));
        return false;
    }
    return true;
}

static void web_close(void* user, u64 stable_id)
{
    (void)user;
    mel_camera_web__close((unsigned)(stable_id & 0xffffffffu), (unsigned)(stable_id >> 32));
    web_session_reap(web_session_find(stable_id));
}

static Mel_Camera_Status web_start(void* user, u64 stable_id)
{
    (void)user;
    if (!web_session_find(stable_id))
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
    if (!mel_camera_web__start((unsigned)(stable_id & 0xffffffffu), (unsigned)(stable_id >> 32)))
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
    return MEL_CAMERA_OK;
}

static Mel_Camera_Status web_stop(void* user, u64 stable_id)
{
    (void)user;
    if (!web_session_find(stable_id))
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
    mel_camera_web__stop((unsigned)(stable_id & 0xffffffffu), (unsigned)(stable_id >> 32));
    return MEL_CAMERA_OK;
}

static void* web_native(void* user, u64 stable_id)
{
    (void)user;
    Web_Session* s = web_session_find(stable_id);
    return s ? (void*)s : NULL;
}

static void web_shutdown(void* user, const Mel_Alloc* alloc)
{
    (void)user;
    (void)alloc;
    for (usize i = 0; i < g_web.sessions.count; i++)
    {
        Web_Session* s = &g_web.sessions.items[i];
        mel_camera_web__close((unsigned)(s->stable_id & 0xffffffffu), (unsigned)(s->stable_id >> 32));
        s->have_sink = false;
        if (s->buf)
            mel_dealloc(g_web.alloc, s->buf);
    }
    if (g_web.sessions.allocator != NULL)
    {
        mel_array_free(&g_web.sessions);
        mel_array_init(&g_web.sessions, NULL);
    }

    web_devices_clear();
    if (g_web.devices.allocator != NULL)
    {
        mel_array_free(&g_web.devices);
        mel_array_init(&g_web.devices, NULL);
    }

    g_web.have_auth_sink = false;
    g_web.auth_sink = (Mel_Camera_Sink){ 0 };
    g_web.alloc = NULL;
}

void mel_camera__register_host_providers(void)
{
    static const Mel_Camera_Provider_Desc desc = {
        .name = "web-getusermedia",
        .enumerate = web_enumerate,
        .open = web_open,
        .close = web_close,
        .start = web_start,
        .stop = web_stop,
        .authorization = web_authorization,
        .authorize = web_authorize,
        .native = web_native,
        .shutdown = web_shutdown,
    };
    mel_camera_provider_register(&desc);
}
