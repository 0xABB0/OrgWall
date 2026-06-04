#include <camera/provider.h>

#include <image/image.h>
#include <image/format.h>
#include <image/geometry.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <string/str8.h>
#include <thread/thread.h>
#include <log/log.h>

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>

#include <linux/videodev2.h>

typedef struct
{
    const mel_image_format* fmt;
    u32                     fourcc;
} V4l2_Format_Map;

static const V4l2_Format_Map* v4l2_format_map(usize* count)
{
    static const V4l2_Format_Map map[] = {
        { &mel_image_nv12, V4L2_PIX_FMT_NV12 },
        { &mel_image_nv21, V4L2_PIX_FMT_NV21 },
        { &mel_image_i420, V4L2_PIX_FMT_YUV420 },
    };
    *count = sizeof map / sizeof map[0];
    return map;
}

static const mel_image_format* v4l2_format_for_fourcc(u32 fourcc)
{
    usize                  n = 0;
    const V4l2_Format_Map* map = v4l2_format_map(&n);
    for (usize i = 0; i < n; i++)
        if (map[i].fourcc == fourcc)
            return map[i].fmt;
    return NULL;
}

static bool v4l2_fourcc_for_format(const mel_image_format* fmt, u32* out)
{
    usize                  n = 0;
    const V4l2_Format_Map* map = v4l2_format_map(&n);
    for (usize i = 0; i < n; i++)
        if (map[i].fmt == fmt)
        {
            *out = map[i].fourcc;
            return true;
        }
    return false;
}

static int v4l2_xioctl(int fd, unsigned long req, void* arg)
{
    int r;
    do
        r = ioctl(fd, req, arg);
    while (r < 0 && errno == EINTR);
    return r;
}

typedef struct
{
    void*  start;
    size_t length;
} V4l2_Buffer;

typedef struct
{
    int                     fd;
    i32                     node;
    u64                     stable_id;
    char                    card[64];
    const mel_image_format* fmt;
    u32                     fourcc;
    i32                     width, height;
    i32                     bytesperline;
    Mel_Array(V4l2_Buffer) buffers;
    Mel_Camera_Modes       modes;

    i32   plane_count;
    i32   y_stride;
    i32   chroma_stride;
    i32   chroma_w, chroma_h;
    usize off_u, off_v;

    Mel_Camera_Sink sink;
    bool            have_sink;
    bool            streaming;

    Mel_Thread   thread;
    bool         thread_started;
    _Atomic(u32) run;
} V4l2_Device;

static struct
{
    const Mel_Alloc* alloc;
    Mel_Array(V4l2_Device*) devices;
    bool scanned;
} g_v4l2;

static const Mel_Alloc* v4l2_alloc(void) { return g_v4l2.alloc ? g_v4l2.alloc : mel_alloc_heap(); }

static u64 v4l2_stable_id(const char* path)
{
    u64 h = 1469598103934665603ULL;
    for (const char* p = path; *p; p++)
    {
        h ^= (u8)*p;
        h *= 1099511628211ULL;
    }
    return h | 1ULL;
}

static V4l2_Device* v4l2_device_by_id(u64 stable_id)
{
    for (usize i = 0; i < g_v4l2.devices.count; i++)
        if (g_v4l2.devices.items[i]->stable_id == stable_id)
            return g_v4l2.devices.items[i];
    return NULL;
}

static bool v4l2_node_is_capture(int fd, char* name, usize name_cap)
{
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof cap);
    if (v4l2_xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
        return false;
    u32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE))
        return false;
    if (!(caps & V4L2_CAP_STREAMING))
        return false;
    snprintf(name, name_cap, "%s", (const char*)cap.card);
    return true;
}

static void v4l2_free_device(V4l2_Device* d)
{
    if (d->fd >= 0)
        close(d->fd);
    mel_array_free(&d->buffers);
    mel_array_free(&d->modes);
    mel_dealloc(v4l2_alloc(), d);
}

static void v4l2_clear_devices(void)
{
    for (usize i = 0; i < g_v4l2.devices.count; i++)
        v4l2_free_device(g_v4l2.devices.items[i]);
    mel_array_clear(&g_v4l2.devices);
}

static void v4l2_collect_modes(int fd, Mel_Camera_Modes* out)
{
    mel_array_clear(out);
    for (u32 fi = 0;; fi++)
    {
        struct v4l2_fmtdesc fmtdesc;
        memset(&fmtdesc, 0, sizeof fmtdesc);
        fmtdesc.index = fi;
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (v4l2_xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0)
            break;

        const mel_image_format* fmt = v4l2_format_for_fourcc(fmtdesc.pixelformat);
        if (!fmt)
            continue;

        for (u32 si = 0;; si++)
        {
            struct v4l2_frmsizeenum fsz;
            memset(&fsz, 0, sizeof fsz);
            fsz.index = si;
            fsz.pixel_format = fmtdesc.pixelformat;
            if (v4l2_xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsz) < 0)
                break;
            if (fsz.type != V4L2_FRMSIZE_TYPE_DISCRETE)
                break;

            Mel_Camera_Mode mode = {
                .format = fmt,
                .width = (i32)fsz.discrete.width,
                .height = (i32)fsz.discrete.height,
                .fps_min = 0.0f,
                .fps_max = 0.0f,
            };

            struct v4l2_frmivalenum fiv;
            memset(&fiv, 0, sizeof fiv);
            fiv.index = 0;
            fiv.pixel_format = fmtdesc.pixelformat;
            fiv.width = fsz.discrete.width;
            fiv.height = fsz.discrete.height;
            if (v4l2_xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fiv) >= 0 && fiv.type == V4L2_FRMIVAL_TYPE_DISCRETE)
            {
                f32 best = 0.0f, worst = 0.0f;
                for (u32 ii = 0;; ii++)
                {
                    struct v4l2_frmivalenum it;
                    memset(&it, 0, sizeof it);
                    it.index = ii;
                    it.pixel_format = fmtdesc.pixelformat;
                    it.width = fsz.discrete.width;
                    it.height = fsz.discrete.height;
                    if (v4l2_xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &it) < 0 || it.type != V4L2_FRMIVAL_TYPE_DISCRETE)
                        break;
                    if (it.discrete.numerator == 0)
                        continue;
                    f32 fps = (f32)it.discrete.denominator / (f32)it.discrete.numerator;
                    if (best == 0.0f || fps > best)
                        best = fps;
                    if (worst == 0.0f || fps < worst)
                        worst = fps;
                }
                mode.fps_min = worst;
                mode.fps_max = best;
            }
            mel_array_push(out, mode);
        }
    }
}

static void v4l2_scan(void)
{
    const Mel_Alloc* a = v4l2_alloc();
    if (g_v4l2.devices.allocator == NULL)
        mel_array_init(&g_v4l2.devices, a);
    v4l2_clear_devices();

    for (i32 idx = 0; idx < 64; idx++)
    {
        char path[32];
        snprintf(path, sizeof path, "/dev/video%d", idx);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        char card[64];
        if (!v4l2_node_is_capture(fd, card, sizeof card))
        {
            close(fd);
            continue;
        }

        V4l2_Device* d = mel_alloc_type(a, V4l2_Device);
        memset(d, 0, sizeof *d);
        d->fd = -1;
        d->node = idx;
        d->stable_id = v4l2_stable_id(path);
        snprintf(d->card, sizeof d->card, "%s", card);
        atomic_store_explicit(&d->run, 0, memory_order_relaxed);
        mel_array_init(&d->buffers, a);
        mel_array_init(&d->modes, a);
        v4l2_collect_modes(fd, &d->modes);
        close(fd);
        mel_array_push(&g_v4l2.devices, d);
        mel_log_info("camera", "v4l2: capture node %s (%s) stable_id=%llu modes=%zu", path, card, (unsigned long long)d->stable_id, d->modes.count);
    }
    g_v4l2.scanned = true;
}

static u32 v4l2_enumerate(void* user, Mel_Camera_Raw* out, u32 cap)
{
    (void)user;
    if (!g_v4l2.scanned)
        v4l2_scan();

    u32 n = 0;
    for (usize i = 0; i < g_v4l2.devices.count && n < cap; i++)
    {
        V4l2_Device* d = g_v4l2.devices.items[i];
        out[n].stable_id = d->stable_id;
        out[n].name = str8_from_cstr(d->card);
        out[n].facing = &mel_camera_unknown;
        out[n].modes = d->modes.items;
        out[n].mode_count = (u32)d->modes.count;
        n++;
    }
    return n;
}

static const mel_camera_auth* v4l2_authorization(void* user)
{
    (void)user;
    return &mel_camera_auth_granted;
}

static void v4l2_authorize(void* user, Mel_Camera_Sink sink)
{
    (void)user;
    if (sink.on_auth)
        sink.on_auth(sink.token, &mel_camera_auth_granted);
}

static void v4l2_unmap(V4l2_Device* d)
{
    for (usize i = 0; i < d->buffers.count; i++)
        if (d->buffers.items[i].start && d->buffers.items[i].start != MAP_FAILED)
            munmap(d->buffers.items[i].start, d->buffers.items[i].length);
    mel_array_clear(&d->buffers);
}

static bool v4l2_open(void* user, u64 stable_id, Mel_Camera_Config cfg, Mel_Camera_Sink sink)
{
    (void)user;
    V4l2_Device* d = v4l2_device_by_id(stable_id);
    if (!d)
    {
        mel_log_error("camera", "v4l2 open: device %llu not found", (unsigned long long)stable_id);
        return false;
    }

    u32 fourcc = 0;
    if (!v4l2_fourcc_for_format(cfg.format, &fourcc))
    {
        mel_log_error("camera", "v4l2 open: pixel format '%s' unsupported by backend (NV12/NV21/YUV420 only)", mel_image_format_name(cfg.format));
        return false;
    }

    if (d->fd < 0)
    {
        char path[32];
        snprintf(path, sizeof path, "/dev/video%d", d->node);
        d->fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (d->fd < 0)
        {
            mel_log_error("camera", "v4l2 open: open(%s) failed: %s", path, strerror(errno));
            return false;
        }
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = (u32)cfg.width;
    fmt.fmt.pix.height = (u32)cfg.height;
    fmt.fmt.pix.pixelformat = fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (v4l2_xioctl(d->fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        mel_log_error("camera", "v4l2 open: VIDIOC_S_FMT failed: %s", strerror(errno));
        goto fail;
    }
    if (fmt.fmt.pix.pixelformat != fourcc || (i32)fmt.fmt.pix.width != cfg.width || (i32)fmt.fmt.pix.height != cfg.height)
    {
        mel_log_error("camera", "v4l2 open: device refused config; got %ux%u fourcc 0x%08x, wanted %dx%d 0x%08x", fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat, cfg.width, cfg.height, fourcc);
        goto fail;
    }

    if (cfg.fps > 0.0f)
    {
        struct v4l2_streamparm parm;
        memset(&parm, 0, sizeof parm);
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = (u32)(cfg.fps + 0.5f);
        if (v4l2_xioctl(d->fd, VIDIOC_S_PARM, &parm) < 0)
            mel_log_warn("camera", "v4l2 open: VIDIOC_S_PARM (fps=%.1f) failed: %s", (double)cfg.fps, strerror(errno));
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof req);
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (v4l2_xioctl(d->fd, VIDIOC_REQBUFS, &req) < 0)
    {
        mel_log_error("camera", "v4l2 open: VIDIOC_REQBUFS failed: %s", strerror(errno));
        goto fail;
    }
    if (req.count < 2)
    {
        mel_log_error("camera", "v4l2 open: insufficient buffer memory (got %u)", req.count);
        goto fail;
    }

    mel_array_clear(&d->buffers);
    for (u32 i = 0; i < req.count; i++)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (v4l2_xioctl(d->fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            mel_log_error("camera", "v4l2 open: VIDIOC_QUERYBUF[%u] failed: %s", i, strerror(errno));
            v4l2_unmap(d);
            goto fail;
        }
        void* p = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, buf.m.offset);
        if (p == MAP_FAILED)
        {
            mel_log_error("camera", "v4l2 open: mmap[%u] failed: %s", i, strerror(errno));
            v4l2_unmap(d);
            goto fail;
        }
        V4l2_Buffer vb = { .start = p, .length = buf.length };
        mel_array_push(&d->buffers, vb);
    }

    d->fmt = cfg.format;
    d->fourcc = fourcc;
    d->width = cfg.width;
    d->height = cfg.height;
    d->bytesperline = (i32)fmt.fmt.pix.bytesperline;

    d->plane_count = mel_image_format_plane_count(d->fmt);
    d->y_stride = d->bytesperline > 0 ? d->bytesperline : d->width;
    d->chroma_w = (d->width + 1) / 2;
    d->chroma_h = (d->height + 1) / 2;
    d->off_u = (usize)d->y_stride * (usize)d->height;
    if (d->plane_count == 2)
    {
        d->chroma_stride = d->y_stride;
        d->off_v = 0;
    }
    else
    {
        d->chroma_stride = d->y_stride / 2;
        d->off_v = d->off_u + (usize)d->chroma_stride * (usize)d->chroma_h;
    }

    d->sink = sink;
    d->have_sink = true;
    return true;

fail:
    close(d->fd);
    d->fd = -1;
    return false;
}

static void v4l2_deliver(V4l2_Device* d, const struct v4l2_buffer* buf)
{
    if (!d->have_sink || d->sink.on_frame == NULL)
        return;

    const u8* base = (const u8*)d->buffers.items[buf->index].start;

    Mel_Image image;
    bool      wrapped = false;

    if (d->plane_count == 2)
    {
        Mel_Image_Plane planes[2];
        planes[0].pixels = (u8*)base;
        planes[0].stride = d->y_stride;
        planes[0].w = d->width;
        planes[0].h = d->height;
        planes[0].bpp = 1;
        planes[1].pixels = (u8*)base + d->off_u;
        planes[1].stride = d->chroma_stride;
        planes[1].w = d->chroma_w;
        planes[1].h = d->chroma_h;
        planes[1].bpp = 2;
        wrapped = mel_image_wrap(&image, d->fmt, d->width, d->height, planes, 2);
    }
    else if (d->plane_count == 3)
    {
        Mel_Image_Plane planes[3];
        planes[0].pixels = (u8*)base;
        planes[0].stride = d->y_stride;
        planes[0].w = d->width;
        planes[0].h = d->height;
        planes[0].bpp = 1;
        planes[1].pixels = (u8*)base + d->off_u;
        planes[1].stride = d->chroma_stride;
        planes[1].w = d->chroma_w;
        planes[1].h = d->chroma_h;
        planes[1].bpp = 1;
        planes[2].pixels = (u8*)base + d->off_v;
        planes[2].stride = d->chroma_stride;
        planes[2].w = d->chroma_w;
        planes[2].h = d->chroma_h;
        planes[2].bpp = 1;
        wrapped = mel_image_wrap(&image, d->fmt, d->width, d->height, planes, 3);
    }

    if (!wrapped)
        return;

    u64              ns = (u64)buf->timestamp.tv_sec * 1000000000ull + (u64)buf->timestamp.tv_usec * 1000ull;
    Mel_Camera_Frame frame = {
        .image = image,
        .timestamp_ns = ns,
        .sequence = 0,
        .orient = { .quarter_turns = 0, .flip_x = false },
    };
    d->sink.on_frame(d->sink.token, &frame);
}

static int v4l2_capture_loop(void* user)
{
    V4l2_Device* d = (V4l2_Device*)user;
    mel_thread_set_name("mel.cam.v4l2");

    while (atomic_load_explicit(&d->run, memory_order_acquire))
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(d->fd, &fds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
        int            r = select(d->fd + 1, &fds, NULL, NULL, &tv);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            mel_log_error("camera", "v4l2 capture: select failed: %s", strerror(errno));
            break;
        }
        if (r == 0)
            continue;

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (v4l2_xioctl(d->fd, VIDIOC_DQBUF, &buf) < 0)
        {
            if (errno == EAGAIN)
                continue;
            mel_log_error("camera", "v4l2 capture: VIDIOC_DQBUF failed: %s", strerror(errno));
            break;
        }
        if (buf.index < d->buffers.count)
            v4l2_deliver(d, &buf);
        if (v4l2_xioctl(d->fd, VIDIOC_QBUF, &buf) < 0)
        {
            mel_log_error("camera", "v4l2 capture: VIDIOC_QBUF failed: %s", strerror(errno));
            break;
        }
    }
    return 0;
}

static Mel_Camera_Status v4l2_start(void* user, u64 stable_id)
{
    (void)user;
    V4l2_Device* d = v4l2_device_by_id(stable_id);
    if (!d)
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
    if (d->streaming)
        return MEL_CAMERA_OK;
    if (d->buffers.count == 0)
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_BUSY;

    for (usize i = 0; i < d->buffers.count; i++)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = (u32)i;
        if (v4l2_xioctl(d->fd, VIDIOC_QBUF, &buf) < 0)
        {
            mel_log_error("camera", "v4l2 start: VIDIOC_QBUF[%zu] failed: %s", i, strerror(errno));
            return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_UNSUPPORTED;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (v4l2_xioctl(d->fd, VIDIOC_STREAMON, &type) < 0)
    {
        mel_log_error("camera", "v4l2 start: VIDIOC_STREAMON failed: %s", strerror(errno));
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_UNSUPPORTED;
    }

    atomic_store_explicit(&d->run, 1, memory_order_release);
    if (!mel_thread_spawn(&d->thread, v4l2_capture_loop, d, .name = "mel.cam.v4l2"))
    {
        atomic_store_explicit(&d->run, 0, memory_order_release);
        v4l2_xioctl(d->fd, VIDIOC_STREAMOFF, &type);
        mel_log_error("camera", "v4l2 start: capture thread spawn failed");
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_UNSUPPORTED;
    }
    d->thread_started = true;
    d->streaming = true;
    return MEL_CAMERA_OK;
}

static Mel_Camera_Status v4l2_stop(void* user, u64 stable_id)
{
    (void)user;
    V4l2_Device* d = v4l2_device_by_id(stable_id);
    if (!d)
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
    if (!d->streaming)
        return MEL_CAMERA_OK;

    atomic_store_explicit(&d->run, 0, memory_order_release);
    if (d->thread_started)
    {
        mel_thread_join(&d->thread, NULL);
        d->thread_started = false;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (v4l2_xioctl(d->fd, VIDIOC_STREAMOFF, &type) < 0)
        mel_log_warn("camera", "v4l2 stop: VIDIOC_STREAMOFF failed: %s", strerror(errno));

    d->streaming = false;
    return MEL_CAMERA_OK;
}

static void v4l2_close(void* user, u64 stable_id)
{
    (void)user;
    V4l2_Device* d = v4l2_device_by_id(stable_id);
    if (!d)
        return;
    if (d->streaming)
        v4l2_stop(user, stable_id);
    v4l2_unmap(d);

    if (d->fd >= 0)
    {
        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof req);
        req.count = 0;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        v4l2_xioctl(d->fd, VIDIOC_REQBUFS, &req);
        close(d->fd);
        d->fd = -1;
    }

    d->have_sink = false;
    d->fmt = NULL;
}

static void* v4l2_native(void* user, u64 stable_id)
{
    (void)user;
    V4l2_Device* d = v4l2_device_by_id(stable_id);
    return (d && d->fd >= 0) ? &d->fd : NULL;
}

static Mel_Camera_Provider_Desc g_desc;

void mel_camera__register_host_providers(void)
{
    g_v4l2.alloc = mel_alloc_heap();
    g_desc = (Mel_Camera_Provider_Desc){
        .name = "linux-v4l2",
        .enumerate = v4l2_enumerate,
        .open = v4l2_open,
        .close = v4l2_close,
        .start = v4l2_start,
        .stop = v4l2_stop,
        .authorization = v4l2_authorization,
        .authorize = v4l2_authorize,
        .native = v4l2_native,
    };
    mel_camera_provider_register(&g_desc);
}
