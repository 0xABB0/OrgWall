#include <core/platform.h>
#include <app/app.h>
#include <app/provider.h>
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
#include <reactor/reactor.h>
#include <thread/mutex.h>
#include <time/nano.h>
#include <vibration/vibration.h>

#include <string.h>

static const char* MEL_TAG = "camera-scanner";

#define PREVIEW_BUFFERS     3
#define DECODE_TEXT_LIMIT   256
#define SYMBOLOGY_LIMIT     32
#define DECODE_EVERY_N      8
#define DECODE_EVERY_N_IDLE 30
#define PREVIEW_MIN_WIDTH   720

#define FLASH_DURATION_NS ((i64)280 * 1000 * 1000)
#define TOAST_DURATION_NS ((i64)1400 * 1000 * 1000)

typedef struct
{
    char text[DECODE_TEXT_LIMIT];
    i32  text_len;
    char symbology[SYMBOLOGY_LIMIT];
    i32  symbology_len;
} Decode_Payload;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Reactor*     reactor;
    Mel_Gui_Handle   canvas;

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

    Decode_Payload front;
    bool           has_decode;
    u64            decode_interval;

    bool tearing_down;

    bool        scanning;
    const char* status_title;
    const char* status_detail;

    i64  flash_origin_ns;
    bool flashing;

    i64  toast_origin_ns;
    bool toast_showing;

    Mel_Rect card_rect;

    Mel_Future* copy_future;
} App_State;

static App_State g_app;

static mel_color8 rgba(u8 r, u8 g, u8 b, u8 a) { return mel_color8_rgba(r, g, b, a); }

static f32 clamp01(f32 x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

static i64 now_ns(void) { return (i64)mel_nanos_since_unspecified_epoch(); }

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

static i32 measure_chars(f32 width_px, f32 font_size)
{
    f32 per = font_size * 0.55f;
    if (per < 1.0f)
        per = 1.0f;
    i32 n = (i32)(width_px / per);
    return n < 1 ? 1 : n;
}

static void draw_elided(Mel_Painter* p, str8 text, Mel_Vec2 pos, mel_color8 c, f32 font, f32 max_w)
{
    i32 budget = measure_chars(max_w, font);
    if ((i32)text.len <= budget)
    {
        mel_painter_draw_text(p, text, pos, c, font);
        return;
    }

    char buf[DECODE_TEXT_LIMIT + 4];
    i32  keep = budget - 1;
    if (keep < 1)
        keep = 1;
    if (keep > DECODE_TEXT_LIMIT)
        keep = DECODE_TEXT_LIMIT;
    if (keep > (i32)text.len)
        keep = (i32)text.len;
    while (keep > 0 && (text.data[keep] & 0xC0) == 0x80)
        --keep;
    memcpy(buf, text.data, (usize)keep);
    buf[keep] = (char)0xE2;
    buf[keep + 1] = (char)0x80;
    buf[keep + 2] = (char)0xA6;
    str8 out = { (u8*)buf, (size)(keep + 3) };
    mel_painter_draw_text(p, out, pos, c, font);
}

static void draw_result_card(Mel_Painter* p, i32 w, i32 ht)
{
    f32 margin = 16.0f;
    f32 card_h = 96.0f;
    f32 card_w = (f32)w - 2.0f * margin;
    if (card_w > 560.0f)
        card_w = 560.0f;
    f32 card_x = ((f32)w - card_w) * 0.5f;
    f32 card_y = (f32)ht - card_h - margin;

    Mel_Rect card = mel_rect(card_x, card_y, card_w, card_h);
    g_app.card_rect = card;

    mel_painter_fill_round_rect(p, card, 14.0f, rgba(24, 24, 28, 235));

    f32 pad = 16.0f;
    f32 inner_x = card_x + pad;
    f32 inner_w = card_w - 2.0f * pad;

    if (g_app.has_decode && g_app.front.text_len > 0)
    {
        str8 badge = { (u8*)g_app.front.symbology, (size)g_app.front.symbology_len };
        f32  badge_w = (f32)g_app.front.symbology_len * 9.0f + 22.0f;
        if (badge_w < 48.0f)
            badge_w = 48.0f;
        Mel_Rect badge_rect = mel_rect(inner_x, card_y + pad, badge_w, 24.0f);
        mel_painter_fill_round_rect(p, badge_rect, 7.0f, rgba(46, 120, 90, 255));
        if (g_app.front.symbology_len > 0)
            mel_painter_draw_text(p, badge, mel_vec2(inner_x + 11.0f, card_y + pad + 5.0f), rgba(235, 255, 244, 255), 13.0f);

        str8 payload = { (u8*)g_app.front.text, (size)g_app.front.text_len };
        draw_elided(p, payload, mel_vec2(inner_x, card_y + pad + 38.0f), rgba(236, 236, 240, 255), 17.0f, inner_w);

        const char* hint = "Tap to copy";
        if (g_app.toast_showing)
        {
            i64 e = now_ns() - g_app.toast_origin_ns;
            if (e < TOAST_DURATION_NS)
                hint = "Copied";
            else
                g_app.toast_showing = false;
        }
        mel_painter_draw_text(p, str8_from_cstr(hint), mel_vec2(inner_x, card_y + card_h - 22.0f), rgba(150, 200, 175, 255), 12.0f);
    }
    else
    {
        mel_painter_draw_text(p, S8("Ready"), mel_vec2(inner_x, card_y + pad + 4.0f), rgba(150, 150, 158, 255), 13.0f);
        mel_painter_draw_text(p, S8("Point the camera at a barcode or QR code"), mel_vec2(inner_x, card_y + pad + 34.0f), rgba(220, 220, 226, 255), 16.0f);
    }
}

static void draw_state_message(Mel_Painter* p, i32 w, i32 ht, const char* title, const char* detail)
{
    f32 cx = (f32)w * 0.5f;
    f32 cy = (f32)ht * 0.42f;
    f32 tw = (f32)strlen(title) * 9.0f;
    mel_painter_draw_text(p, str8_from_cstr(title), mel_vec2(cx - tw * 0.5f, cy), rgba(235, 235, 240, 255), 18.0f);
    if (detail && detail[0])
    {
        f32 dw = (f32)strlen(detail) * 6.5f;
        mel_painter_draw_text(p, str8_from_cstr(detail), mel_vec2(cx - dw * 0.5f, cy + 28.0f), rgba(170, 170, 178, 255), 14.0f);
    }
}

static void on_paint(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 ht, void* user)
{
    (void)h;
    (void)user;

    mel_painter_clear(p, rgba(10, 10, 12, 255));

    Mel_Rect view = mel_rect(0, 0, (f32)w, (f32)ht);

    if (g_app.preview_ready)
    {
        Mel_Image* img = &g_app.preview[g_app.idx_consume];
        view = letterbox((f32)w, (f32)ht, (f32)img->w, (f32)img->h);
        mel_painter_draw_image(p, img, view, g_app.alloc);
    }

    if (g_app.scanning)
    {
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

        draw_result_card(p, w, ht);
    }
    else
    {
        const char* title = g_app.status_title ? g_app.status_title : "Camera unavailable";
        draw_state_message(p, w, ht, title, g_app.status_detail);
    }
}

static void on_gui_update(void* user)
{
    App_State* app = user;

    bool           got_frame = false;
    bool           got_text = false;
    Decode_Payload payload;

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
        app->shared_dirty = false;
        got_text = true;
    }
    mel_mutex_unlock(&app->swap_lock);

    if (got_frame)
        app->preview_ready = true;

    if (got_text && payload.text_len > 0)
    {
        bool changed = !app->has_decode || payload.text_len != app->front.text_len || memcmp(payload.text, app->front.text, (usize)payload.text_len) != 0;
        app->front = payload;
        app->has_decode = true;

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
                i32 n = r.text_len;
                if (n > DECODE_TEXT_LIMIT)
                    n = DECODE_TEXT_LIMIT;

                const char* sym = r.symbology ? r.symbology : "CODE";
                i32         sn = (i32)strlen(sym);
                if (sn > SYMBOLOGY_LIMIT)
                    sn = SYMBOLOGY_LIMIT;

                mel_mutex_lock(&app->swap_lock);
                bool same = app->shared.text_len == n && (n == 0 || memcmp(app->shared.text, r.text, (usize)n) == 0);
                if (n > 0)
                    memcpy(app->shared.text, r.text, (usize)n);
                app->shared.text_len = n;
                memcpy(app->shared.symbology, sym, (usize)sn);
                app->shared.symbology_len = sn;
                app->shared_dirty = true;
                mel_mutex_unlock(&app->swap_lock);

                app->decode_interval = same ? DECODE_EVERY_N_IDLE : DECODE_EVERY_N;
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
        mel_reactor_post(app->reactor, on_gui_update, app);
}

static void set_status(const char* title, const char* detail)
{
    g_app.scanning = false;
    g_app.status_title = title;
    g_app.status_detail = detail;
    if (!mel_gui_handle_is_none(g_app.canvas))
        mel_gui_invalidate(g_app.canvas);
}

static void set_scanning(void)
{
    g_app.scanning = true;
    g_app.status_title = NULL;
    g_app.status_detail = NULL;
    if (!mel_gui_handle_is_none(g_app.canvas))
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
    mel_log_info(MEL_TAG, "device \"%.*s\" mode %dx%d @ %.0ffps fmt %s facing %s", (int)d.value.name.len, (const char*)d.value.name.data, w, h, (double)mode.fps_max, mel_image_format_name(mode.format), mel_camera_facing_name(d.value.facing));
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

static void on_pointer_down(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)h;
    App_State* app = user;
    if (!app->has_decode || app->front.text_len <= 0)
        return;
    if (!mel_rect_contains_point(app->card_rect, mel_vec2((f32)x, (f32)y)))
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

    app->toast_showing = true;
    app->toast_origin_ns = now_ns();
    mel_gui_invalidate(app->canvas);
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
    mel_mutex_destroy(&g_app.swap_lock);
    mel_clip_shutdown();
    mel_vib_shutdown();
    mel_camera_shutdown();
}

static void build_main(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 0, .margin = 0, .cross_align = MEL_ALIGN_STRETCH));

    g_app.canvas = mel_canvas_create(frame, .on_.on_paint = on_paint, .pointer.on_pointer_down = on_pointer_down, .lifecycle.on_destroy = on_destroy, .user = &g_app, .layoutable = { .preferred_h = 480, .weight = 1 });

    const Mel_Alloc* a = g_app.alloc;
    g_app.auth_future = mel_camera_authorize(a);
    if (g_app.auth_future == NULL)
    {
        mel_log_error(MEL_TAG, "authorize returned no future (camera uninitialized or OOM)");
        set_status("Camera unavailable", "Camera subsystem is unavailable.");
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
    g_app.decode_interval = DECODE_EVERY_N;
    g_app.scanning = false;
    g_app.status_title = "Requesting camera permission";
    g_app.status_detail = "Allow access to start scanning.";
    mel_mutex_init(&g_app.swap_lock, MEL_MUTEX_PLAIN);

    mel_camera_init(g_app.alloc, reactor);
    mel_vib_init(g_app.alloc, reactor);
    mel_vib_refresh();
    mel_clip_init(g_app.alloc, reactor);

    mel_app_register_screen(S8("main"), build_main, NULL);
    mel_app_present(S8("main"), NULL);
}
