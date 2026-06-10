#include <core/platform.h>
#include <gui/gui.h>
#include <gui/gui.h>

#include <allocator/heap.h>
#include <barcode/decode.h>
#include <camera/camera.h>
#include <clipboard/clipboard.h>
#include <image/image.h>
#include <log/log.h>
#include <paint/painter.h>
#include <future/future.h>
#include <executor/executor.h>
#include <vat/vat.h>
#include <collection/list.h>
#include <thread/mutex.h>
#include <time/nano.h>
#include <vibration/vibration.h>

#include <string.h>

static const char* MEL_TAG = "camera-scanner";

#define PREVIEW_BUFFERS     3
#define DECODE_EVERY_N      8
#define DECODE_EVERY_N_IDLE 30
#define PREVIEW_MIN_WIDTH   720

#define FLASH_DURATION_NS   ((i64)280 * 1000 * 1000)

typedef struct
{
    char*       text;
    i32         text_len;
    const char* symbology;
} Decode_Payload;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Vat*         vat;
    Mel_Task         gui_update_task;

    Mel_Gui_Handle canvas;
    Mel_Gui_Handle status_label;
    Mel_Gui_Handle detail_label;
    Mel_Gui_Handle copy_button;

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

    Decode_Payload shared;
    bool           shared_dirty;

    Decode_Payload last;
    u64            decode_interval;

    Decode_Payload front;
    bool           has_decode;

    bool tearing_down;
    bool scanning;

    i64  flash_origin_ns;
    bool flashing;

    Mel_Future* copy_future;
} App_State;

static App_State g_app;

static mel_color8 rgba(u8 r, u8 g, u8 b, u8 a) { return mel_color8_rgba(r, g, b, a); }

static f32 clamp01(f32 x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

static i64 now_ns(void) { return (i64)mel_nanos_since_unspecified_epoch(); }

static void payload_free(Decode_Payload* p, const Mel_Alloc* a)
{
    if (p->text != NULL)
        mel_dealloc(a, p->text);
    p->text = NULL;
    p->text_len = 0;
    p->symbology = NULL;
}

static Mel_Rect letterbox(f32 win_w, f32 win_h, f32 img_w, f32 img_h)
{
    if (img_w <= 0.0f || img_h <= 0.0f)
        return mel_rect(0, 0, win_w, win_h);

    f32 scale = win_w / img_w;
    f32 sh = win_h / img_h;
    if (sh < scale)
        scale = sh;

    f32 w = img_w * scale;
    f32 h = img_h * scale;
    f32 x = (win_w - w) * 0.5f;
    f32 y = (win_h - h) * 0.5f;
    return mel_rect(x, y, w, h);
}

static void draw_corner_ticks(Mel_Painter* p, Mel_Rect r, f32 len, f32 thick, mel_color8 c)
{
    f32 x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;

    mel_painter_draw_line(p, mel_vec2(x0, y0), mel_vec2(x0 + len, y0), c, thick);
    mel_painter_draw_line(p, mel_vec2(x0, y0), mel_vec2(x0, y0 + len), c, thick);

    mel_painter_draw_line(p, mel_vec2(x1, y0), mel_vec2(x1 - len, y0), c, thick);
    mel_painter_draw_line(p, mel_vec2(x1, y0), mel_vec2(x1, y0 + len), c, thick);

    mel_painter_draw_line(p, mel_vec2(x0, y1), mel_vec2(x0 + len, y1), c, thick);
    mel_painter_draw_line(p, mel_vec2(x0, y1), mel_vec2(x0, y1 - len), c, thick);

    mel_painter_draw_line(p, mel_vec2(x1, y1), mel_vec2(x1 - len, y1), c, thick);
    mel_painter_draw_line(p, mel_vec2(x1, y1), mel_vec2(x1, y1 - len), c, thick);
}

static Mel_Rect reticle_rect(f32 w, f32 h)
{
    f32 side = w < h ? w : h;
    f32 r = side * 0.62f;
    f32 x = (w - r) * 0.5f;
    f32 y = (h - r) * 0.5f - h * 0.04f;
    return mel_rect(x, y, r, r);
}

static void on_paint(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 ht, void* user)
{
    (void)h;
    (void)user;

    mel_painter_clear(p, rgba(10, 10, 12, 255));

    if (g_app.preview_ready)
    {
        Mel_Image* img = &g_app.preview[g_app.idx_consume];
        Mel_Rect   view = letterbox((f32)w, (f32)ht, (f32)img->w, (f32)img->h);
        mel_painter_draw_image(p, img, view, g_app.alloc);
    }

    if (!g_app.scanning)
        return;

    Mel_Rect ret = reticle_rect((f32)w, (f32)ht);
    f32      radius = 18.0f;

    mel_color8 dim = rgba(0, 0, 0, 120);
    mel_painter_fill_rect(p, mel_rect(0, 0, (f32)w, ret.y), dim);
    mel_painter_fill_rect(p, mel_rect(0, ret.y + ret.h, (f32)w, (f32)ht - (ret.y + ret.h)), dim);
    mel_painter_fill_rect(p, mel_rect(0, ret.y, ret.x, ret.h), dim);
    mel_painter_fill_rect(p, mel_rect(ret.x + ret.w, ret.y, (f32)w - (ret.x + ret.w), ret.h), dim);

    f32        flash = 0.0f;
    mel_color8 frame_col = rgba(245, 245, 250, 220);
    if (g_app.flashing)
    {
        i64 e = now_ns() - g_app.flash_origin_ns;
        if (e < FLASH_DURATION_NS)
        {
            flash = 1.0f - clamp01((f32)e / (f32)FLASH_DURATION_NS);
            u8 a = (u8)(160.0f + 95.0f * flash);
            frame_col = rgba((u8)(80 - 40 * flash), 235, (u8)(140 + 30 * flash), a);
        }
        else
        {
            g_app.flashing = false;
        }
    }

    if (flash > 0.0f)
    {
        u8       glow = (u8)(70.0f * flash);
        Mel_Rect grow = mel_rect(ret.x - 6.0f, ret.y - 6.0f, ret.w + 12.0f, ret.h + 12.0f);
        mel_painter_fill_round_rect(p, grow, radius + 6.0f, rgba(60, 230, 130, glow));
    }

    mel_painter_stroke_rect(p, ret, frame_col, 2.0f);
    draw_corner_ticks(p, ret, ret.w * 0.12f, 4.0f, frame_col);
}

static void show_decode(const Decode_Payload* d)
{
    str8 text = { (u8*)d->text, (size)d->text_len };
    mel_gui_set_text(g_app.status_label, str8_from_cstr(d->symbology ? d->symbology : "CODE"));
    mel_gui_set_text(g_app.detail_label, text);
    mel_gui_set_text(g_app.copy_button, S8("Copy to clipboard"));
    mel_gui_set_enabled(g_app.copy_button, true);
}

static void on_gui_update(Mel_Task* self)
{
    App_State* app = mel_container_of(self, App_State, gui_update_task);

    bool           got_frame = false;
    bool           got_text = false;
    Decode_Payload payload = { 0 };

    mel_mutex_lock(&app->swap_lock);
    if (app->tearing_down)
    {
        mel_mutex_unlock(&app->swap_lock);
        return;
    }
    if (app->pending)
    {
        i32 tmp = app->idx_consume;
        app->idx_consume = app->idx_spare;
        app->idx_spare = tmp;
        app->pending = false;
        got_frame = true;
    }
    if (app->shared_dirty)
    {
        payload = app->shared;
        app->shared = (Decode_Payload){ 0 };
        app->shared_dirty = false;
        got_text = true;
    }
    mel_mutex_unlock(&app->swap_lock);

    if (got_frame)
        app->preview_ready = true;

    if (got_text && payload.text_len > 0)
    {
        bool changed = !app->has_decode || payload.text_len != app->front.text_len || memcmp(payload.text, app->front.text, (usize)payload.text_len) != 0;
        payload_free(&app->front, app->alloc);
        app->front = payload;
        app->has_decode = true;

        show_decode(&app->front);

        if (changed)
        {
            app->flashing = true;
            app->flash_origin_ns = now_ns();

            Mel_Vib_Device dev = MEL_VIB_DEVICE_NULL;
            if (mel_vib_count() > 0 && mel_vib_list(&dev, 1) > 0)
            {
                Mel_Vib_Event       ev = mel_vib_pulse(0.7f, 0.6f, 0.04f);
                Mel_Vib_Pattern     pat = { .events = &ev, .count = 1, .loop = 0 };
                Mel_Vib_Play_Result pr = mel_vib_play(dev, &pat);
                if (mel_vib_failed(pr.status))
                    mel_log_warn(MEL_TAG, "haptic pulse failed (status 0x%x)", pr.status);
            }
        }
    }
    else if (got_text)
    {
        payload_free(&payload, app->alloc);
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
    {
        if (app->preview_w == ow && app->preview_h == oh)
            return true;

        mel_log_warn(MEL_TAG, "preview extent changed %dx%d -> %dx%d, reallocating", app->preview_w, app->preview_h, ow, oh);
        mel_mutex_lock(&app->swap_lock);
        app->pending = false;
        app->preview_ready = false;
        mel_mutex_unlock(&app->swap_lock);
        for (i32 i = 0; i < PREVIEW_BUFFERS; ++i)
            mel_image_free(&app->preview[i]);
    }

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
    {
        if (app->raw.w == fw && app->raw.h == fh)
            return true;
        mel_log_warn(MEL_TAG, "orient scratch extent changed %dx%d -> %dx%d, reallocating", app->raw.w, app->raw.h, fw, fh);
        mel_image_free(&app->raw);
        app->raw_ready = false;
    }
    if (!mel_image_init(&app->raw, &mel_image_rgba8, fw, fh, app->alloc))
    {
        mel_log_error(MEL_TAG, "orient scratch init failed (%dx%d)", fw, fh);
        return false;
    }
    app->raw_ready = true;
    return true;
}

static void publish_decode(App_State* app, mel_barcode_decode_result* r)
{
    bool same = app->last.text_len == r->text_len && (r->text_len == 0 || memcmp(app->last.text, r->text, (usize)r->text_len) == 0);

    payload_free(&app->last, app->alloc);
    app->last.text = r->text;
    app->last.text_len = r->text_len;
    app->last.symbology = r->symbology;
    r->text = NULL;

    Decode_Payload pub = { 0 };
    pub.symbology = app->last.symbology;
    if (app->last.text_len > 0)
    {
        str8 dup = str8_dup_alloc((str8){ (u8*)app->last.text, (size)app->last.text_len }, app->alloc);
        pub.text = (char*)dup.data;
        pub.text_len = (i32)dup.len;
    }

    mel_mutex_lock(&app->swap_lock);
    payload_free(&app->shared, app->alloc);
    app->shared = pub;
    app->shared_dirty = true;
    mel_mutex_unlock(&app->swap_lock);

    app->decode_interval = same ? DECODE_EVERY_N_IDLE : DECODE_EVERY_N;
}

static void on_frame(const Mel_Camera_Frame* frame, void* user)
{
    App_State* app = user;
    bool       changed = false;

    if (app->decoder_ready && (frame->sequence % app->decode_interval) == 0)
    {
        mel_image_gray gray = mel_image_gray_borrow(&frame->image);
        if (gray.pixels != NULL)
        {
            mel_barcode_decode_result r;
            if (mel_barcode_decoder_decode(&app->decoder, &gray, &r))
            {
                publish_decode(app, &r);
                changed = true;
                mel_barcode_decode_result_free(&r, app->alloc);
            }
            else
            {
                app->decode_interval = DECODE_EVERY_N;
            }
        }
    }

    bool busy;
    mel_mutex_lock(&app->swap_lock);
    busy = app->pending;
    mel_mutex_unlock(&app->swap_lock);

    Mel_Image_Orient orient = frame->orient;

    if (!busy && ensure_preview(app, frame->image.w, frame->image.h, orient))
    {
        Mel_Image* dst = &app->preview[app->idx_produce];
        bool       identity = (orient.quarter_turns == 0 && !orient.flip_x);
        bool       produced = false;

        if (identity)
            produced = mel_image_convert_scratch(&frame->image, dst, app->alloc);
        else if (ensure_raw(app, frame->image.w, frame->image.h))
            produced = mel_image_convert_scratch(&frame->image, &app->raw, app->alloc) && mel_image_orient(&app->raw, dst, orient);

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
        mel_vat_post(app->vat, &app->gui_update_task);
}

static void set_status(const char* title, const char* detail)
{
    g_app.scanning = false;
    if (!mel_gui_handle_is_none(g_app.status_label))
        mel_gui_set_text(g_app.status_label, str8_from_cstr(title));
    if (!mel_gui_handle_is_none(g_app.detail_label))
        mel_gui_set_text(g_app.detail_label, str8_from_cstr(detail));
    if (!mel_gui_handle_is_none(g_app.copy_button))
        mel_gui_set_enabled(g_app.copy_button, false);
    if (!mel_gui_handle_is_none(g_app.canvas))
        mel_gui_invalidate(g_app.canvas);
}

static void set_scanning(void)
{
    g_app.scanning = true;
    mel_gui_set_text(g_app.status_label, S8("Ready"));
    mel_gui_set_text(g_app.detail_label, S8("Point the camera at a barcode or QR code"));
    mel_gui_invalidate(g_app.canvas);
}

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
        set_status("No camera found", "Connect a camera and relaunch.");
        return;
    }

    Mel_Camera cam = MEL_CAMERA_NULL;
    mel_camera_list(&cam, 1);

    Mel_Camera_Describe_Result d = mel_camera_describe(cam, a);
    if (mel_camera_status_failed(d.status) || d.value.modes.count == 0)
    {
        mel_log_error(MEL_TAG, "camera describe failed or has no modes");
        set_status("Camera unavailable", "Camera reports no usable modes.");
        mel_camera_describe_free(&d);
        return;
    }

    Mel_Camera_Mode mode;
    if (!pick_mode(&d.value.modes, &mode))
    {
        mel_log_error(MEL_TAG, "no luma-bearing camera mode (need nv12/nv21/i420/gray8)");
        set_status("Camera unavailable", "Camera has no luma mode for scanning.");
        mel_camera_describe_free(&d);
        return;
    }

    Mel_Camera_Config cfg = { .format = mode.format, .width = mode.width, .height = mode.height, .fps = mode.fps_max };
    i32               w = mode.width, h = mode.height;
    mel_log_info(MEL_TAG,
                 "device \"%.*s\" mode %dx%d @ %.0ffps fmt %s facing %s",
                 (int)d.value.name.len,
                 (const char*)d.value.name.data,
                 w,
                 h,
                 (double)mode.fps_max,
                 mel_image_format_name(mode.format),
                 mel_camera_facing_name(d.value.facing));
    mel_camera_describe_free(&d);

    if (!mel_barcode_decoder_init(&g_app.decoder, w, a))
    {
        mel_log_error(MEL_TAG, "decoder init failed");
        set_status("Camera unavailable", "Could not initialize the decoder.");
        return;
    }
    g_app.decoder_ready = true;

    Mel_Future* of = mel_camera_open(cam, cfg, a);
    if (of == NULL || mel_camera_status_failed(mel_camera_future_status(of)))
    {
        mel_log_error(MEL_TAG, "camera open failed");
        set_status("Camera unavailable", "Could not open the camera.");
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
        set_status("Camera unavailable", "Could not start the camera stream.");
        if (sf)
            mel_camera_future_free(sf);
        return;
    }
    mel_camera_future_free(sf);

    set_scanning();
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
        set_status("Camera access denied", "Enable camera access in system settings.");
    }
}

static void on_copy_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    App_State* app = user;
    if (!app->has_decode || app->front.text_len <= 0)
        return;

    if (app->copy_future)
    {
        mel_clip_future_free(app->copy_future);
        app->copy_future = NULL;
    }

    str8 payload = { (u8*)app->front.text, (size)app->front.text_len };
    app->copy_future = mel_clip_write_text(payload, .alloc = app->alloc);
    if (app->copy_future == NULL)
    {
        mel_log_warn(MEL_TAG, "clipboard write returned no future");
        return;
    }

    mel_gui_set_text(app->copy_button, S8("Copied"));
}

static void main_destroy(Mel_Gui_Handle frame, void* arg)
{
    (void)frame;
    (void)arg;

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

    mel_mutex_lock(&g_app.swap_lock);
    g_app.tearing_down = true;
    g_app.pending = false;
    g_app.preview_ready = false;
    mel_mutex_unlock(&g_app.swap_lock);

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
    if (g_app.copy_future)
    {
        mel_clip_future_free(g_app.copy_future);
        g_app.copy_future = NULL;
    }
    payload_free(&g_app.shared, g_app.alloc);
    payload_free(&g_app.last, g_app.alloc);
    payload_free(&g_app.front, g_app.alloc);
    g_app.has_decode = false;
    mel_mutex_destroy(&g_app.swap_lock);
    mel_clip_shutdown();
    mel_vib_shutdown();
    mel_camera_shutdown();
}

static void build_main(Mel_Gui_Handle frame, void* user)
{
    (void)user;

    mel_gui_set_text(frame, S8("Scanner"));
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 0, .margin = 0, .cross_align = MEL_ALIGN_STRETCH));

    g_app.canvas = mel_canvas_create(frame, .on_.on_paint = on_paint, .user = &g_app, .layoutable = { .preferred_h = 480, .weight = 1 });

    Mel_Gui_Handle card = mel_panel_create(frame, .layout = mel_column_layout(.spacing = 6, .margin = 12, .cross_align = MEL_ALIGN_STRETCH), .layoutable = { .preferred_h = 132 });
    g_app.status_label = mel_label_create(card, .text = S8("Requesting camera permission"), .layoutable = { .preferred_h = 22 });
    g_app.detail_label = mel_label_create(card, .text = S8("Allow access to start scanning."), .layoutable = { .preferred_h = 24 });
    g_app.copy_button = mel_button_create(card, .text = S8("Copy to clipboard"), .disabled = true, .pointer.on_click = on_copy_clicked, .user = &g_app, .layoutable = { .preferred_h = 40 });

    const Mel_Alloc* a = g_app.alloc;
    g_app.auth_future = mel_camera_authorize(a);
    if (g_app.auth_future == NULL)
    {
        mel_log_error(MEL_TAG, "authorize returned no future (camera uninitialized or OOM)");
        set_status("Camera unavailable", "Camera subsystem is unavailable.");
        return;
    }

    mel_task_init(&g_app.auth_task, on_auth_resolved);
    mel_future_then(g_app.auth_future, &g_app.auth_task, mel_vat_executor(g_app.vat));
}

void mel_app_setup(Mel_Vat* root)
{
    mel_gui_init(root);

    g_app.alloc = mel_alloc_heap();
    g_app.vat = root;
    mel_task_init(&g_app.gui_update_task, on_gui_update);
    g_app.idx_produce = 0;
    g_app.idx_spare = 1;
    g_app.idx_consume = 2;
    g_app.decode_interval = DECODE_EVERY_N;
    g_app.scanning = false;
    mel_mutex_init(&g_app.swap_lock, MEL_MUTEX_PLAIN);

    mel_camera_init(g_app.alloc, mel_vat_executor(root));
    mel_vib_init(g_app.alloc, root);
    mel_vib_refresh();
    mel_clip_init(g_app.alloc, root);

    mel_app_register_screen(S8("main"), .build = build_main, .on_destroy = main_destroy);
    mel_app_present(S8("main"), NULL);
}
