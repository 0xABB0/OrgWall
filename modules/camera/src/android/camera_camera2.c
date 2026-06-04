#include <camera/provider.h>
#include <camera/android/android.h>

#include <image/image.h>
#include <image/format.h>
#include <image/geometry.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <log/log.h>

#include <platform/android/jni.h>

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraMetadataTags.h>
#include <camera/NdkCaptureRequest.h>
#include <camera/NdkCameraCaptureSession.h>
#include <media/NdkImageReader.h>
#include <android/native_window.h>

#include <pthread.h>
#include <string.h>

#define MEL_CAM2_YUV420 AIMAGE_FORMAT_YUV_420_888
#define MEL_CAM2_READER_BUFFERS 3

typedef struct
{
    u64                          stable_id;
    char                         id[8];
    const mel_camera_facing*     facing;
    Mel_Array(Mel_Camera_Mode)   modes;
    i32                          sensor_orientation;
} Cam2_Device;

typedef struct
{
    u64                          stable_id;
    const mel_image_format*      fmt;
    Mel_Camera_Sink              sink;
    bool                         have_sink;
    Mel_Image_Orient             orient;

    pthread_mutex_t              lock;

    i32                          width, height;
    i32                          cwidth, cheight;
    bool                         layout_resolved;
    const mel_image_format*      delivered_fmt;
    i32                          chroma_bpp;
    Mel_Image_Plane              planes[3];
    u32                          plane_count;
    bool                         logged_frame_error;
    bool                         logged_acquire_error;
    bool                         logged_capture_error;

    ACameraDevice*               device;
    AImageReader*                reader;
    ANativeWindow*               window;
    ACaptureSessionOutput*       out;
    ACaptureSessionOutputContainer* container;
    ACameraOutputTarget*         target;
    ACaptureRequest*             request;
    ACameraCaptureSession*       session;
    bool                         streaming;
} Cam2_Session;

typedef struct
{
    const Mel_Alloc*             alloc;
    ACameraManager*              manager;
    Mel_Array(Cam2_Device)       devices;
    Mel_Array(Cam2_Session*)     sessions;
    Mel_Camera_Sink              auth_sink;
    bool                         have_auth_sink;
} Cam2;

static Cam2 g_cam2;

static u64 cam2_hash_id(const char* s)
{
    u64 h = 1469598103934665603ull;
    for (const char* p = s; *p; p++)
    {
        h ^= (u64)(u8)*p;
        h *= 1099511628211ull;
    }
    return h;
}

static ACameraManager* cam2_manager(void)
{
    if (g_cam2.manager == NULL)
        g_cam2.manager = ACameraManager_create();
    return g_cam2.manager;
}

static const mel_camera_facing* cam2_facing_from_lens(u8 lens)
{
    switch (lens)
    {
    case ACAMERA_LENS_FACING_FRONT:
        return &mel_camera_front;
    case ACAMERA_LENS_FACING_BACK:
        return &mel_camera_back;
    case ACAMERA_LENS_FACING_EXTERNAL:
        return &mel_camera_external;
    default:
        return &mel_camera_unknown;
    }
}

static void cam2_device_free(Cam2_Device* d) { mel_array_free(&d->modes); }

static void cam2_devices_clear(void)
{
    for (usize i = 0; i < g_cam2.devices.count; i++)
        cam2_device_free(&g_cam2.devices.items[i]);
    mel_array_clear(&g_cam2.devices);
}

static void cam2_read_modes(ACameraMetadata* meta, Cam2_Device* d)
{
    mel_array_init(&d->modes, g_cam2.alloc);

    ACameraMetadata_const_entry e = { 0 };
    if (ACameraMetadata_getConstEntry(meta, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &e) != ACAMERA_OK)
        return;

    for (u32 i = 0; i + 3 < e.count; i += 4)
    {
        i32 format = e.data.i32[i + 0];
        i32 w = e.data.i32[i + 1];
        i32 h = e.data.i32[i + 2];
        i32 dir = e.data.i32[i + 3];
        if (format != MEL_CAM2_YUV420 || dir != ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT)
            continue;
        Mel_Camera_Mode m = { .format = &mel_image_nv12, .width = w, .height = h, .fps_min = 0.0f, .fps_max = 0.0f };
        mel_array_push(&d->modes, m);
    }
}

static void cam2_refresh_devices(void)
{
    cam2_devices_clear();

    ACameraManager* mgr = cam2_manager();
    if (mgr == NULL)
        return;

    ACameraIdList* list = NULL;
    if (ACameraManager_getCameraIdList(mgr, &list) != ACAMERA_OK || list == NULL)
    {
        mel_log_error("camera", "camera2: getCameraIdList failed");
        return;
    }

    for (int i = 0; i < list->numCameras; i++)
    {
        const char* id = list->cameraIds[i];
        if (id == NULL || strlen(id) >= sizeof(((Cam2_Device*)0)->id))
            continue;

        ACameraMetadata* meta = NULL;
        if (ACameraManager_getCameraCharacteristics(mgr, id, &meta) != ACAMERA_OK || meta == NULL)
            continue;

        Cam2_Device d;
        memset(&d, 0, sizeof d);
        d.stable_id = cam2_hash_id(id);
        memcpy(d.id, id, strlen(id));
        d.facing = &mel_camera_unknown;
        d.sensor_orientation = 0;

        ACameraMetadata_const_entry fe = { 0 };
        if (ACameraMetadata_getConstEntry(meta, ACAMERA_LENS_FACING, &fe) == ACAMERA_OK && fe.count > 0)
            d.facing = cam2_facing_from_lens(fe.data.u8[0]);

        ACameraMetadata_const_entry oe = { 0 };
        if (ACameraMetadata_getConstEntry(meta, ACAMERA_SENSOR_ORIENTATION, &oe) == ACAMERA_OK && oe.count > 0)
            d.sensor_orientation = oe.data.i32[0];

        cam2_read_modes(meta, &d);
        ACameraMetadata_free(meta);

        mel_array_push(&g_cam2.devices, d);
    }

    ACameraManager_deleteCameraIdList(list);
}

static Cam2_Device* cam2_device_by_id(u64 stable_id)
{
    for (usize i = 0; i < g_cam2.devices.count; i++)
        if (g_cam2.devices.items[i].stable_id == stable_id)
            return &g_cam2.devices.items[i];
    return NULL;
}

static u32 cam2_enumerate(void* user, Mel_Camera_Raw* out, u32 cap)
{
    (void)user;
    cam2_refresh_devices();

    u32 n = (u32)g_cam2.devices.count;
    if (cap == 0)
        return n;

    u32 fill = n < cap ? n : cap;
    for (u32 i = 0; i < fill; i++)
    {
        Cam2_Device* d = &g_cam2.devices.items[i];
        out[i].stable_id = d->stable_id;
        out[i].name = (str8){ (u8*)d->id, (size)strlen(d->id) };
        out[i].facing = d->facing;
        out[i].modes = d->modes.items;
        out[i].mode_count = (u32)d->modes.count;
    }
    return n;
}

static Cam2_Session* cam2_session_by_id(u64 stable_id)
{
    for (usize i = 0; i < g_cam2.sessions.count; i++)
        if (g_cam2.sessions.items[i]->stable_id == stable_id)
            return g_cam2.sessions.items[i];
    return NULL;
}

static bool cam2_resolve_layout(Cam2_Session* s, uint8_t* u_data, uint8_t* v_data, int32_t u_pix, int32_t v_pix)
{
    if (u_pix == 2 && v_pix == 2)
    {
        ptrdiff_t delta = v_data - u_data;
        if (delta == 1)
        {
            s->delivered_fmt = &mel_image_nv12;
            s->chroma_bpp = 2;
            s->plane_count = 2;
        }
        else if (delta == -1)
        {
            s->delivered_fmt = &mel_image_nv21;
            s->chroma_bpp = 2;
            s->plane_count = 2;
        }
        else
        {
            mel_log_warn("camera", "camera2: interleaved chroma with non-adjacent U/V (delta=%td); wrapping as nv12 from U base", delta);
            s->delivered_fmt = &mel_image_nv12;
            s->chroma_bpp = 2;
            s->plane_count = 2;
        }
        return true;
    }
    if (u_pix == 1 && v_pix == 1)
    {
        s->delivered_fmt = &mel_image_i420;
        s->chroma_bpp = 1;
        s->plane_count = 3;
        return true;
    }
    mel_log_error("camera", "camera2: unsupported chroma pixel stride u=%d v=%d", u_pix, v_pix);
    return false;
}

static void cam2_emit_image(Cam2_Session* s, AImage* image)
{
    if (!s->have_sink || s->sink.on_frame == NULL)
        return;

    uint8_t* y_data = NULL;
    uint8_t* u_data = NULL;
    uint8_t* v_data = NULL;
    int      y_len = 0, u_len = 0, v_len = 0;
    int32_t  y_row = 0, u_row = 0, v_row = 0;

    if (AImage_getPlaneData(image, 0, &y_data, &y_len) != AMEDIA_OK ||
        AImage_getPlaneData(image, 1, &u_data, &u_len) != AMEDIA_OK ||
        AImage_getPlaneData(image, 2, &v_data, &v_len) != AMEDIA_OK ||
        AImage_getPlaneRowStride(image, 0, &y_row) != AMEDIA_OK ||
        AImage_getPlaneRowStride(image, 1, &u_row) != AMEDIA_OK ||
        AImage_getPlaneRowStride(image, 2, &v_row) != AMEDIA_OK)
    {
        if (!s->logged_frame_error)
        {
            mel_log_error("camera", "camera2: frame plane query failed; dropping frames");
            s->logged_frame_error = true;
        }
        return;
    }

    if (!s->layout_resolved)
    {
        int32_t u_pix = 0, v_pix = 0;
        if (AImage_getPlanePixelStride(image, 1, &u_pix) != AMEDIA_OK ||
            AImage_getPlanePixelStride(image, 2, &v_pix) != AMEDIA_OK)
        {
            if (!s->logged_frame_error)
            {
                mel_log_error("camera", "camera2: chroma pixel-stride query failed; dropping frames");
                s->logged_frame_error = true;
            }
            return;
        }
        if (!cam2_resolve_layout(s, u_data, v_data, u_pix, v_pix))
            return;
        s->layout_resolved = true;
    }

    i32 w = s->width, h = s->height;
    i32 cw = s->cwidth, ch = s->cheight;

    s->planes[0] = (Mel_Image_Plane){ .pixels = y_data, .stride = y_row, .w = w, .h = h, .bpp = 1 };
    if (s->delivered_fmt == &mel_image_nv12)
        s->planes[1] = (Mel_Image_Plane){ .pixels = u_data, .stride = u_row, .w = cw, .h = ch, .bpp = s->chroma_bpp };
    else if (s->delivered_fmt == &mel_image_nv21)
        s->planes[1] = (Mel_Image_Plane){ .pixels = v_data, .stride = v_row, .w = cw, .h = ch, .bpp = s->chroma_bpp };
    else
    {
        s->planes[1] = (Mel_Image_Plane){ .pixels = u_data, .stride = u_row, .w = cw, .h = ch, .bpp = s->chroma_bpp };
        s->planes[2] = (Mel_Image_Plane){ .pixels = v_data, .stride = v_row, .w = cw, .h = ch, .bpp = s->chroma_bpp };
    }

    Mel_Image image_wrap;
    if (!mel_image_wrap(&image_wrap, s->delivered_fmt, w, h, s->planes, s->plane_count))
        return;

    int64_t ts = 0;
    if (AImage_getTimestamp(image, &ts) != AMEDIA_OK && !s->logged_frame_error)
    {
        mel_log_error("camera", "camera2: AImage_getTimestamp failed; shipping ts=0");
        s->logged_frame_error = true;
    }

    Mel_Camera_Frame frame = {
        .image = image_wrap,
        .timestamp_ns = (u64)ts,
        .sequence = 0,
        .orient = s->orient,
    };
    s->sink.on_frame(s->sink.token, &frame);
}

static void cam2_on_image(void* context, AImageReader* reader)
{
    Cam2_Session* s = (Cam2_Session*)context;
    if (s == NULL)
        return;

    pthread_mutex_lock(&s->lock);
    if (!s->have_sink)
    {
        pthread_mutex_unlock(&s->lock);
        return;
    }

    AImage*       image = NULL;
    media_status_t st = AImageReader_acquireLatestImage(reader, &image);
    if (st != AMEDIA_OK || image == NULL)
    {
        if (!s->logged_acquire_error)
        {
            mel_log_error("camera", "camera2: acquireLatestImage failed (status=%d); frames stalling", (int)st);
            s->logged_acquire_error = true;
        }
        pthread_mutex_unlock(&s->lock);
        return;
    }

    cam2_emit_image(s, image);
    AImage_delete(image);
    pthread_mutex_unlock(&s->lock);
}

static void cam2_dev_on_disconnected(void* ctx, ACameraDevice* dev)
{
    (void)ctx;
    (void)dev;
    mel_log_warn("camera", "camera2: device disconnected");
}

static void cam2_dev_on_error(void* ctx, ACameraDevice* dev, int err)
{
    (void)ctx;
    (void)dev;
    mel_log_error("camera", "camera2: device error %d", err);
}

static void cam2_session_on_closed(void* ctx, ACameraCaptureSession* sess)
{
    (void)ctx;
    (void)sess;
    mel_log_debug("camera", "camera2: capture session closed");
}

static void cam2_session_on_ready(void* ctx, ACameraCaptureSession* sess)
{
    (void)ctx;
    (void)sess;
}

static void cam2_session_on_active(void* ctx, ACameraCaptureSession* sess)
{
    (void)ctx;
    (void)sess;
}

static void cam2_on_capture_failed(void* ctx, ACameraCaptureSession* sess, ACaptureRequest* req, ACameraCaptureFailure* failure)
{
    (void)sess;
    (void)req;
    Cam2_Session* s = (Cam2_Session*)ctx;
    if (s == NULL || s->logged_capture_error)
        return;
    s->logged_capture_error = true;
    mel_log_error("camera", "camera2: capture failed (reason=%d, frame=%lld); stream may stall",
                  failure ? failure->reason : -1, failure ? (long long)failure->frameNumber : -1);
}

static void cam2_session_teardown(Cam2_Session* s)
{
    if (s->reader != NULL)
        AImageReader_setImageListener(s->reader, NULL);

    pthread_mutex_lock(&s->lock);
    s->have_sink = false;

    if (s->session != NULL)
    {
        if (s->streaming)
            ACameraCaptureSession_stopRepeating(s->session);
        ACameraCaptureSession_close(s->session);
        s->session = NULL;
    }
    s->streaming = false;
    if (s->request != NULL)
    {
        ACaptureRequest_free(s->request);
        s->request = NULL;
    }
    if (s->target != NULL)
    {
        ACameraOutputTarget_free(s->target);
        s->target = NULL;
    }
    if (s->container != NULL)
    {
        ACaptureSessionOutputContainer_free(s->container);
        s->container = NULL;
    }
    if (s->out != NULL)
    {
        ACaptureSessionOutput_free(s->out);
        s->out = NULL;
    }
    if (s->reader != NULL)
    {
        AImageReader_delete(s->reader);
        s->reader = NULL;
    }
    s->window = NULL;
    if (s->device != NULL)
    {
        ACameraDevice_close(s->device);
        s->device = NULL;
    }
    pthread_mutex_unlock(&s->lock);
}

static void cam2_session_free(Cam2_Session* s)
{
    cam2_session_teardown(s);
    pthread_mutex_destroy(&s->lock);
    mel_dealloc(g_cam2.alloc, s);
}

static bool cam2_open(void* user, u64 stable_id, Mel_Camera_Config cfg, Mel_Camera_Sink sink)
{
    (void)user;
    if (cam2_session_by_id(stable_id) != NULL)
    {
        mel_log_error("camera", "camera2: device %llu already open", (unsigned long long)stable_id);
        return false;
    }

    Cam2_Device* d = cam2_device_by_id(stable_id);
    if (d == NULL)
    {
        cam2_refresh_devices();
        d = cam2_device_by_id(stable_id);
    }
    if (d == NULL)
    {
        mel_log_error("camera", "camera2: open: device %llu not found", (unsigned long long)stable_id);
        return false;
    }

    if (cfg.format != &mel_image_nv12 && cfg.format != &mel_image_nv21 && cfg.format != &mel_image_i420)
    {
        mel_log_error("camera", "camera2: open: unsupported pixel format (need nv12/nv21/i420)");
        return false;
    }

    ACameraManager* mgr = cam2_manager();
    if (mgr == NULL)
        return false;

    Cam2_Session* s = mel_alloc_type(g_cam2.alloc, Cam2_Session);
    if (s == NULL)
        return false;
    memset(s, 0, sizeof *s);
    s->stable_id = stable_id;
    s->fmt = cfg.format;
    s->sink = sink;
    s->have_sink = true;
    s->width = cfg.width;
    s->height = cfg.height;
    s->cwidth = (cfg.width + 1) / 2;
    s->cheight = (cfg.height + 1) / 2;
    s->orient = (Mel_Image_Orient){ .quarter_turns = d->sensor_orientation / 90, .flip_x = (d->facing == &mel_camera_front) };
    if (pthread_mutex_init(&s->lock, NULL) != 0)
    {
        mel_log_error("camera", "camera2: mutex init failed");
        mel_dealloc(g_cam2.alloc, s);
        return false;
    }

    if (AImageReader_new(cfg.width, cfg.height, MEL_CAM2_YUV420, MEL_CAM2_READER_BUFFERS, &s->reader) != AMEDIA_OK || s->reader == NULL)
    {
        mel_log_error("camera", "camera2: AImageReader_new failed (%dx%d)", cfg.width, cfg.height);
        cam2_session_free(s);
        return false;
    }

    if (AImageReader_getWindow(s->reader, &s->window) != AMEDIA_OK || s->window == NULL)
    {
        mel_log_error("camera", "camera2: AImageReader_getWindow failed");
        cam2_session_free(s);
        return false;
    }

    ACameraDevice_StateCallbacks dev_cbs = {
        .context = NULL,
        .onDisconnected = cam2_dev_on_disconnected,
        .onError = cam2_dev_on_error,
    };
    if (ACameraManager_openCamera(mgr, d->id, &dev_cbs, &s->device) != ACAMERA_OK || s->device == NULL)
    {
        mel_log_error("camera", "camera2: openCamera failed for id=%s (permission denied?)", d->id);
        cam2_session_free(s);
        return false;
    }

    if (ACaptureSessionOutputContainer_create(&s->container) != ACAMERA_OK ||
        ACaptureSessionOutput_create(s->window, &s->out) != ACAMERA_OK ||
        ACaptureSessionOutputContainer_add(s->container, s->out) != ACAMERA_OK)
    {
        mel_log_error("camera", "camera2: output container setup failed");
        cam2_session_free(s);
        return false;
    }

    ACameraCaptureSession_stateCallbacks sess_cbs = {
        .context = NULL,
        .onClosed = cam2_session_on_closed,
        .onReady = cam2_session_on_ready,
        .onActive = cam2_session_on_active,
    };
    if (ACameraDevice_createCaptureSession(s->device, s->container, &sess_cbs, &s->session) != ACAMERA_OK || s->session == NULL)
    {
        mel_log_error("camera", "camera2: createCaptureSession failed");
        cam2_session_free(s);
        return false;
    }

    if (ACameraDevice_createCaptureRequest(s->device, TEMPLATE_PREVIEW, &s->request) != ACAMERA_OK || s->request == NULL)
    {
        mel_log_error("camera", "camera2: createCaptureRequest failed");
        cam2_session_free(s);
        return false;
    }

    if (ACameraOutputTarget_create(s->window, &s->target) != ACAMERA_OK ||
        ACaptureRequest_addTarget(s->request, s->target) != ACAMERA_OK)
    {
        mel_log_error("camera", "camera2: addTarget failed");
        cam2_session_free(s);
        return false;
    }

    if (cfg.fps > 0.0f)
    {
        int32_t range[2] = { (int32_t)(cfg.fps + 0.5f), (int32_t)(cfg.fps + 0.5f) };
        ACaptureRequest_setEntry_i32(s->request, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, range);
    }

    mel_array_push(&g_cam2.sessions, s);
    AImageReader_ImageListener listener = { .context = s, .onImageAvailable = cam2_on_image };
    AImageReader_setImageListener(s->reader, &listener);
    return true;
}

static void cam2_close(void* user, u64 stable_id)
{
    (void)user;
    for (usize i = 0; i < g_cam2.sessions.count; i++)
    {
        Cam2_Session* s = g_cam2.sessions.items[i];
        if (s->stable_id != stable_id)
            continue;
        mel_array_remove_unordered(&g_cam2.sessions, i);
        cam2_session_free(s);
        return;
    }
}

static Mel_Camera_Status cam2_start(void* user, u64 stable_id)
{
    (void)user;
    Cam2_Session* s = cam2_session_by_id(stable_id);
    if (s == NULL || s->session == NULL || s->request == NULL)
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
    if (s->streaming)
        return MEL_CAMERA_OK;
    ACameraCaptureSession_captureCallbacks cap_cbs = {
        .context = s,
        .onCaptureFailed = cam2_on_capture_failed,
    };
    if (ACameraCaptureSession_setRepeatingRequest(s->session, &cap_cbs, 1, &s->request, NULL) != ACAMERA_OK)
    {
        mel_log_error("camera", "camera2: setRepeatingRequest failed");
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_BUSY;
    }
    s->streaming = true;
    return MEL_CAMERA_OK;
}

static Mel_Camera_Status cam2_stop(void* user, u64 stable_id)
{
    (void)user;
    Cam2_Session* s = cam2_session_by_id(stable_id);
    if (s == NULL || s->session == NULL)
        return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
    if (!s->streaming)
        return MEL_CAMERA_OK;
    ACameraCaptureSession_stopRepeating(s->session);
    s->streaming = false;
    return MEL_CAMERA_OK;
}

static const mel_camera_auth* cam2_authorization(void* user)
{
    (void)user;
    return mel_camera_android_permission_granted() ? &mel_camera_auth_granted : &mel_camera_auth_not_determined;
}

static void cam2_authorize(void* user, Mel_Camera_Sink sink)
{
    (void)user;
    if (sink.on_auth == NULL)
        return;
    if (mel_camera_android_permission_granted())
    {
        sink.on_auth(sink.token, &mel_camera_auth_granted);
        return;
    }
    g_cam2.auth_sink = sink;
    g_cam2.have_auth_sink = true;
    if (!mel_camera_android_request_permission())
    {
        g_cam2.have_auth_sink = false;
        sink.on_auth(sink.token, &mel_camera_auth_denied);
    }
}

static void* cam2_native(void* user, u64 stable_id)
{
    (void)user;
    Cam2_Session* s = cam2_session_by_id(stable_id);
    return s ? (void*)s->device : NULL;
}

void mel_camera_android_on_permission(bool granted)
{
    if (!g_cam2.have_auth_sink || g_cam2.auth_sink.on_auth == NULL)
        return;
    Mel_Camera_Sink sink = g_cam2.auth_sink;
    g_cam2.have_auth_sink = false;
    sink.on_auth(sink.token, granted ? &mel_camera_auth_granted : &mel_camera_auth_denied);
}

void mel_camera__register_host_providers(void)
{
    g_cam2.alloc = mel_alloc_heap();
    mel_array_init(&g_cam2.devices, g_cam2.alloc);
    mel_array_init(&g_cam2.sessions, g_cam2.alloc);

    static const Mel_Camera_Provider_Desc desc = {
        .name = "android-camera2",
        .enumerate = cam2_enumerate,
        .open = cam2_open,
        .close = cam2_close,
        .start = cam2_start,
        .stop = cam2_stop,
        .authorization = cam2_authorization,
        .authorize = cam2_authorize,
        .native = cam2_native,
    };
    mel_camera_provider_register(&desc);
}
