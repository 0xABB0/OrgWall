#include <core/platform.h>
#include <app/app.h>
#include <app/provider.h>
#include <gui/gui.h>

#include <allocator/heap.h>
#include <barcode/decode.h>
#include <camera/camera.h>
#include <image/image.h>
#include <log/log.h>
#include <paint/painter.h>
#include <future/future.h>
#include <executor/executor.h>
#include <reactor/reactor.h>
#include <thread/mutex.h>

#include <string.h>

static const char* MEL_TAG = "camera-scanner";

#define PREVIEW_BUFFERS   3
#define DECODE_TEXT_LIMIT 256
#define DECODE_EVERY_N    8
#define PREVIEW_MIN_WIDTH 720

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Reactor*     reactor;
    Mel_Gui_Handle   canvas;
    Mel_Gui_Handle   label;

    mel_barcode_decoder decoder;
    bool                decoder_ready;

    Mel_Camera           cam;
    Mel_Camera_Frame_Sub sub;
    bool                 cam_opened;
    bool                 cam_subscribed;

    Mel_Future* auth_future;
    Mel_Task    auth_task;

    Mel_Image raw;
    bool      raw_ready;

    Mel_Image preview[PREVIEW_BUFFERS];
    i32       preview_w, preview_h;
    bool      preview_ready;

    Mel_Mutex swap_lock;
    i32       idx_produce;
    i32       idx_spare;
    i32       idx_consume;
    bool      pending;

    char text_shared[DECODE_TEXT_LIMIT];
    i32  text_shared_len;
    bool text_dirty;

    char text_front[DECODE_TEXT_LIMIT];
    i32  text_front_len;
    bool has_decode;
} App_State;

static App_State g_app;

static void on_paint(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 ht, void* user)
{
    (void)h;
    (void)user;

    mel_painter_clear(p, mel_color8_rgb(16, 16, 16));

    if (g_app.preview_ready)
        mel_painter_draw_image(p, &g_app.preview[g_app.idx_consume], mel_rect(0, 0, (f32)w, (f32)ht), g_app.alloc);
    else
        mel_painter_draw_text(p, S8("Waiting for camera..."), mel_vec2(16, 28), mel_color8_rgb(200, 200, 200), 16.0f);

    if (g_app.has_decode && g_app.text_front_len > 0)
    {
        Mel_Rect bar = mel_rect(0, (f32)ht - 44.0f, (f32)w, 44.0f);
        mel_painter_fill_rect(p, bar, mel_color8_rgb(0, 0, 0));
        str8 t = { (u8*)g_app.text_front, (size)g_app.text_front_len };
        mel_painter_draw_text(p, t, mel_vec2(12, (f32)ht - 28.0f), mel_color8_rgb(90, 255, 120), 18.0f);
    }
}

static void on_gui_update(void* user)
{
    App_State* app = user;

    bool got_frame = false;
    bool got_text = false;
    char text[DECODE_TEXT_LIMIT];
    i32  text_len = 0;

    mel_mutex_lock(&app->swap_lock);
    if (app->pending)
    {
        i32 tmp = app->idx_consume;
        app->idx_consume = app->idx_spare;
        app->idx_spare = tmp;
        app->pending = false;
        got_frame = true;
    }
    if (app->text_dirty)
    {
        text_len = app->text_shared_len;
        if (text_len > 0)
            memcpy(text, app->text_shared, (usize)text_len);
        app->text_dirty = false;
        got_text = true;
    }
    mel_mutex_unlock(&app->swap_lock);

    if (got_frame)
        app->preview_ready = true;

    if (got_text && text_len > 0)
    {
        memcpy(app->text_front, text, (usize)text_len);
        app->text_front_len = text_len;
        app->has_decode = true;
        str8 t = { (u8*)app->text_front, (size)text_len };
        mel_gui_set_text(app->label, t);
    }

    if (got_frame || got_text)
        mel_gui_invalidate(app->canvas);
}

static bool ensure_preview(App_State* app, i32 fw, i32 fh, Mel_Image_Orient orient)
{
    i32  q = ((orient.quarter_turns % 4) + 4) % 4;
    bool swap = (q == 1 || q == 3);
    i32  ow = swap ? fh : fw;
    i32  oh = swap ? fw : fh;

    if (app->preview[0].format != NULL)
        return app->preview_w == ow && app->preview_h == oh;

    for (i32 i = 0; i < PREVIEW_BUFFERS; ++i)
    {
        if (!mel_image_init(&app->preview[i], &mel_image_rgba8, ow, oh, app->alloc))
        {
            mel_log_error(MEL_TAG, "preview buffer init failed (%dx%d)", ow, oh);
            return false;
        }
    }
    app->preview_w = ow;
    app->preview_h = oh;
    return true;
}

static bool ensure_raw(App_State* app, i32 fw, i32 fh)
{
    if (app->raw_ready)
        return app->raw.w == fw && app->raw.h == fh;
    if (!mel_image_init(&app->raw, &mel_image_rgba8, fw, fh, app->alloc))
    {
        mel_log_error(MEL_TAG, "orient scratch init failed (%dx%d)", fw, fh);
        return false;
    }
    app->raw_ready = true;
    return true;
}

static void on_frame(const Mel_Camera_Frame* frame, void* user)
{
    App_State* app = user;
    bool       changed = false;

    if (app->decoder_ready && (frame->sequence % DECODE_EVERY_N) == 0)
    {
        mel_image_gray gray = mel_image_gray_borrow(&frame->image);
        if (gray.pixels != NULL)
        {
            mel_barcode_decode_result r;
            if (mel_barcode_decoder_decode(&app->decoder, &gray, &r))
            {
                i32 n = r.text_len;
                if (n > DECODE_TEXT_LIMIT)
                    n = DECODE_TEXT_LIMIT;
                mel_mutex_lock(&app->swap_lock);
                if (n > 0)
                    memcpy(app->text_shared, r.text, (usize)n);
                app->text_shared_len = n;
                app->text_dirty = true;
                mel_mutex_unlock(&app->swap_lock);
                changed = true;
                mel_barcode_decode_result_free(&r, app->alloc);
            }
        }
    }

    if (ensure_preview(app, frame->image.w, frame->image.h, frame->orient))
    {
        Mel_Image* dst = &app->preview[app->idx_produce];
        bool       identity = (frame->orient.quarter_turns == 0 && !frame->orient.flip_x);
        bool       produced = false;

        if (identity)
            produced = mel_image_convert_scratch(&frame->image, dst, app->alloc);
        else if (ensure_raw(app, frame->image.w, frame->image.h))
            produced = mel_image_convert_scratch(&frame->image, &app->raw, app->alloc) && mel_image_orient(&app->raw, dst, frame->orient);

        if (produced)
        {
            mel_mutex_lock(&app->swap_lock);
            i32 tmp = app->idx_produce;
            app->idx_produce = app->idx_spare;
            app->idx_spare = tmp;
            app->pending = true;
            mel_mutex_unlock(&app->swap_lock);
            changed = true;
        }
    }

    if (changed)
        mel_reactor_post(app->reactor, on_gui_update, app);
}

static void set_status(const char* msg) { mel_gui_set_text(g_app.label, str8_from_cstr(msg)); }

static bool pick_mode(const Mel_Camera_Modes* modes, Mel_Camera_Mode* out)
{
    Mel_Camera_Mode best = { 0 };
    bool            have = false;

    for (usize i = 0; i < modes->count; ++i)
    {
        Mel_Camera_Mode m = modes->items[i];
        if (!mel_image_format_has_luma(m.format))
            continue;

        if (!have)
        {
            best = m;
            have = true;
            continue;
        }

        bool best_ok = best.width >= PREVIEW_MIN_WIDTH;
        bool m_ok = m.width >= PREVIEW_MIN_WIDTH;

        if (m_ok && best_ok)
        {
            if (m.width < best.width)
                best = m;
        }
        else if (m_ok && !best_ok)
        {
            best = m;
        }
        else if (!m_ok && !best_ok)
        {
            if (m.width > best.width)
                best = m;
        }
    }

    if (have)
        *out = best;
    return have;
}

static void start_camera(void)
{
    const Mel_Alloc* a = g_app.alloc;

    u32 count = mel_camera_count();
    if (count == 0)
    {
        mel_log_error(MEL_TAG, "no camera devices");
        set_status("No camera available.");
        return;
    }

    Mel_Camera cam = MEL_CAMERA_NULL;
    mel_camera_list(&cam, 1);

    Mel_Camera_Describe_Result d = mel_camera_describe(cam, a);
    if (mel_camera_status_failed(d.status) || d.value.modes.count == 0)
    {
        mel_log_error(MEL_TAG, "camera describe failed or has no modes");
        set_status("Camera has no usable modes.");
        mel_camera_describe_free(&d);
        return;
    }

    Mel_Camera_Mode mode;
    if (!pick_mode(&d.value.modes, &mode))
    {
        mel_log_error(MEL_TAG, "no luma-bearing camera mode (need nv12/nv21/i420/gray8)");
        set_status("Camera has no usable luma mode.");
        mel_camera_describe_free(&d);
        return;
    }

    Mel_Camera_Config cfg = { .format = mode.format, .width = mode.width, .height = mode.height, .fps = mode.fps_max };
    i32               w = mode.width, h = mode.height;
    mel_log_info(MEL_TAG, "device \"%.*s\" mode %dx%d @ %.0ffps fmt %s", (int)d.value.name.len, (const char*)d.value.name.data, w, h, (double)mode.fps_max, mel_image_format_name(mode.format));
    mel_camera_describe_free(&d);

    if (!mel_barcode_decoder_init(&g_app.decoder, w, a))
    {
        mel_log_error(MEL_TAG, "decoder init failed");
        set_status("Decoder init failed.");
        return;
    }
    g_app.decoder_ready = true;

    Mel_Future* of = mel_camera_open(cam, cfg, a);
    if (of == NULL || mel_camera_status_failed(mel_camera_future_status(of)))
    {
        mel_log_error(MEL_TAG, "camera open failed");
        set_status("Could not open camera.");
        if (of)
            mel_camera_future_free(of);
        return;
    }
    mel_camera_future_free(of);
    g_app.cam = cam;
    g_app.cam_opened = true;

    g_app.sub = mel_camera_frame_subscribe(cam, on_frame, &g_app);
    g_app.cam_subscribed = true;

    Mel_Future* sf = mel_camera_start(cam, a);
    if (sf == NULL || mel_camera_status_failed(mel_camera_future_status(sf)))
    {
        mel_log_error(MEL_TAG, "camera start failed");
        set_status("Could not start camera.");
        if (sf)
            mel_camera_future_free(sf);
        return;
    }
    mel_camera_future_free(sf);

    set_status("Scanning... point a barcode or QR code at the camera.");
    mel_log_info(MEL_TAG, "streaming %dx%d", w, h);
}

static void on_auth_resolved(Mel_Task* self)
{
    (void)self;
    App_State* app = &g_app;

    bool granted = mel_camera_auth_is_granted(mel_camera_future_auth(app->auth_future));
    mel_camera_future_free(app->auth_future);
    app->auth_future = NULL;

    if (granted)
    {
        start_camera();
    }
    else
    {
        mel_log_error(MEL_TAG, "camera authorization denied");
        set_status("Camera permission denied.");
    }
}

static void on_destroy(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;

    if (g_app.cam_subscribed)
    {
        mel_camera_frame_unsubscribe(g_app.cam, g_app.sub);
        g_app.cam_subscribed = false;
    }
    if (g_app.cam_opened)
    {
        mel_camera_close(g_app.cam);
        g_app.cam_opened = false;
    }
    for (i32 i = 0; i < PREVIEW_BUFFERS; ++i)
    {
        if (g_app.preview[i].format)
            mel_image_free(&g_app.preview[i]);
    }
    if (g_app.raw_ready)
    {
        mel_image_free(&g_app.raw);
        g_app.raw_ready = false;
    }
    if (g_app.decoder_ready)
    {
        mel_barcode_decoder_free(&g_app.decoder);
        g_app.decoder_ready = false;
    }
    if (g_app.auth_future)
    {
        mel_camera_future_free(g_app.auth_future);
        g_app.auth_future = NULL;
    }
    mel_mutex_destroy(&g_app.swap_lock);
    mel_camera_shutdown();
}

static void build_main(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 8, .margin = 12, .cross_align = MEL_ALIGN_STRETCH));

    g_app.canvas = mel_canvas_create(frame, .on_.on_paint = on_paint, .lifecycle.on_destroy = on_destroy, .layoutable = { .preferred_h = 360, .weight = 1 });
    g_app.label = mel_label_create(frame, .text = S8("Requesting camera permission..."), .layoutable = { .preferred_h = 28 });

    const Mel_Alloc* a = g_app.alloc;
    g_app.auth_future = mel_camera_authorize(a);
    if (g_app.auth_future == NULL)
    {
        mel_log_error(MEL_TAG, "authorize returned no future (camera uninitialized or OOM)");
        set_status("Camera unavailable.");
        return;
    }

    mel_task_init(&g_app.auth_task, on_auth_resolved);
    mel_future_then(g_app.auth_future, &g_app.auth_task, mel_reactor_executor(g_app.reactor));
}

void mel_app_setup(Mel_Reactor* reactor)
{
    mel_gui_init(reactor);

    g_app.alloc = mel_alloc_heap();
    g_app.reactor = reactor;
    g_app.idx_produce = 0;
    g_app.idx_spare = 1;
    g_app.idx_consume = 2;
    mel_mutex_init(&g_app.swap_lock, MEL_MUTEX_PLAIN);

    mel_camera_init(g_app.alloc, reactor);

    mel_app_register_screen(S8("main"), build_main, NULL);
    mel_app_present(S8("main"), NULL);
}
