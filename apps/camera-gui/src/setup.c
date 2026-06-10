#include <core/platform.h>
#include <gui/gui.h>

#include <allocator/heap.h>
#include <camera/camera.h>
#include <collection/array.h>
#include <collection/list.h>
#include <executor/executor.h>
#include <future/future.h>
#include <image/image.h>
#include <log/log.h>
#include <paint/painter.h>
#include <thread/mutex.h>
#include <time/nano.h>
#include <vat/vat.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char* MEL_TAG = "camera-gui";

#define PREVIEW_BUFFERS 3
#define HOTPLUG_LINES   4

#if MEL_PLATFORM_ANDROID || MEL_PLATFORM_IOS
#define BENCH_MOBILE 1
#define BENCH_BTN_H  44
#define BENCH_MODE_H 40
#define BENCH_CHK_H  32
#else
#define BENCH_MOBILE 0
#define BENCH_BTN_H  32
#define BENCH_MODE_H 28
#define BENCH_CHK_H  24
#endif

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Vat*         vat;
    Mel_Task         gui_update_task;

    Mel_Gui_Handle frame;
    Mel_Gui_Handle canvas;
    Mel_Gui_Handle auth_label;
    Mel_Gui_Handle authorize_btn;
    Mel_Gui_Handle devices_box;
    Mel_Gui_Handle devices_list;
    Mel_Gui_Handle device_label;
    Mel_Gui_Handle modes_box;
    Mel_Gui_Handle modes_list;
    Mel_Gui_Handle mode_label;
    Mel_Gui_Handle open_btn;
    Mel_Gui_Handle start_btn;
    Mel_Gui_Handle stop_btn;
    Mel_Gui_Handle close_btn;
    Mel_Gui_Handle mirror_check;
    Mel_Gui_Handle state_label;
    Mel_Gui_Handle frames_label;
    Mel_Gui_Handle last_label;
    Mel_Gui_Handle hotplug_labels[HOTPLUG_LINES];

    Mel_Array(Mel_Gui_Handle) device_btns;
    Mel_Array(Mel_Gui_Handle) mode_btns;
    Mel_Array(Mel_Camera) devices;
    Mel_Array(Mel_Camera_Mode) modes;

    i32 selected_dev;
    i32 selected_mode;

    bool authorized;

    Mel_Camera           cam;
    Mel_Camera_Frame_Sub sub;
    bool                 opened;
    bool                 started;
    bool                 subscribed;

    Mel_Future* auth_future;
    Mel_Task    auth_task;

    Mel_Camera_Hotplug_Sub hotplug_sub;
    bool                   hotplug_subscribed;
    u64                    hotplug_seq;
    char                   hotplug_text[HOTPLUG_LINES][128];

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
    bool      tearing_down;
    bool      mirror;

    u64         stat_frames;
    u64         stat_seq;
    i32         stat_w, stat_h;
    const char* stat_fmt;

    u64 fps_anchor_frames;
    i64 fps_anchor_ns;
    f64 fps;
} App_State;

static App_State g_app;

static i64 now_ns(void) { return (i64)mel_nanos_since_unspecified_epoch(); }

static void set_textf(Mel_Gui_Handle h, const char* fmt, ...)
{
    char    buf[256];
    va_list args;
    va_start(args, fmt);
    i32 n = vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    if (n < 0)
        n = 0;
    if ((usize)n >= sizeof buf)
        n = (i32)(sizeof buf) - 1;
    mel_gui_set_text(h, (str8){ (u8*)buf, (size)n });
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
    return mel_rect((win_w - w) * 0.5f, (win_h - h) * 0.5f, w, h);
}

static void on_paint(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 ht, void* user)
{
    (void)h;
    (void)user;

    mel_painter_clear(p, mel_color8_rgba(12, 12, 14, 255));

    if (!g_app.preview_ready)
        return;

    Mel_Image* img = &g_app.preview[g_app.idx_consume];
    Mel_Rect   view = letterbox((f32)w, (f32)ht, (f32)img->w, (f32)img->h);
    mel_painter_draw_image(p, img, view, g_app.alloc);
    mel_painter_stroke_rect(p, view, mel_color8_rgba(70, 70, 80, 255), 1.0f);
}

static void update_buttons(void)
{
    bool dev = g_app.selected_dev >= 0;
    bool mode = g_app.selected_mode >= 0;

    mel_gui_set_enabled(g_app.authorize_btn, !g_app.authorized && g_app.auth_future == NULL);
    mel_gui_set_enabled(g_app.open_btn, g_app.authorized && dev && mode && !g_app.opened);
    mel_gui_set_enabled(g_app.start_btn, g_app.opened && !g_app.started);
    mel_gui_set_enabled(g_app.stop_btn, g_app.started);
    mel_gui_set_enabled(g_app.close_btn, g_app.opened);
}

static void on_gui_update(Mel_Task* self)
{
    App_State* app = mel_container_of(self, App_State, gui_update_task);

    bool        got_frame = false;
    u64         frames, seq;
    i32         fw, fh;
    const char* fmt;

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
    frames = app->stat_frames;
    seq = app->stat_seq;
    fw = app->stat_w;
    fh = app->stat_h;
    fmt = app->stat_fmt;
    mel_mutex_unlock(&app->swap_lock);

    if (got_frame)
        app->preview_ready = true;

    i64 now = now_ns();
    if (app->fps_anchor_ns == 0)
    {
        app->fps_anchor_ns = now;
        app->fps_anchor_frames = frames;
    }
    else if (now - app->fps_anchor_ns >= 500 * 1000 * 1000)
    {
        f64 dt = (f64)(now - app->fps_anchor_ns) / 1e9;
        app->fps = (f64)(frames - app->fps_anchor_frames) / dt;
        app->fps_anchor_ns = now;
        app->fps_anchor_frames = frames;
    }

    set_textf(app->frames_label, "Frames: %llu   (%.1f fps)", (unsigned long long)frames, app->fps);
    if (fmt != NULL)
        set_textf(app->last_label, "Last: seq %llu  %dx%d %s", (unsigned long long)seq, fw, fh, fmt);

    if (got_frame)
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

    bool busy, mirror;
    mel_mutex_lock(&app->swap_lock);
    busy = app->pending;
    mirror = app->mirror;
    app->stat_frames += 1;
    app->stat_seq = frame->sequence;
    app->stat_w = frame->image.w;
    app->stat_h = frame->image.h;
    app->stat_fmt = mel_image_format_name(frame->image.format);
    mel_mutex_unlock(&app->swap_lock);

    Mel_Image_Orient orient = frame->orient;
    if (mirror)
        orient.flip_x = !orient.flip_x;

    bool produced = false;
    if (!busy && ensure_preview(app, frame->image.w, frame->image.h, orient))
    {
        Mel_Image* dst = &app->preview[app->idx_produce];
        bool       identity = (orient.quarter_turns == 0 && !orient.flip_x);

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
        }
    }

    mel_vat_post(app->vat, &app->gui_update_task);
}

static void clear_modes(void)
{
    for (usize i = 0; i < g_app.mode_btns.count; ++i)
        mel_gui_destroy(g_app.mode_btns.items[i]);
    mel_array_clear(&g_app.mode_btns);
    mel_array_clear(&g_app.modes);
    g_app.selected_mode = -1;
    mel_gui_set_text(g_app.mode_label, S8("No mode selected"));
}

static void on_mode_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    i32 idx = (i32)(usize)user;
    if (idx < 0 || (usize)idx >= g_app.modes.count)
        return;

    g_app.selected_mode = idx;
    Mel_Camera_Mode m = g_app.modes.items[idx];
    set_textf(g_app.mode_label, "Mode: %dx%d @%.0f-%.0f %s", m.width, m.height, (f64)m.fps_min, (f64)m.fps_max, mel_image_format_name(m.format));
    update_buttons();
}

static void rebuild_modes(void)
{
    clear_modes();
    if (g_app.selected_dev < 0)
        return;

    Mel_Camera                 cam = g_app.devices.items[g_app.selected_dev];
    Mel_Camera_Describe_Result d = mel_camera_describe(cam, g_app.alloc);
    if (mel_camera_status_failed(d.status))
    {
        mel_log_warn(MEL_TAG, "describe failed for selected device (status 0x%x)", d.status);
        mel_camera_describe_free(&d);
        return;
    }

    for (usize i = 0; i < d.value.modes.count; ++i)
        mel_array_push(&g_app.modes, d.value.modes.items[i]);
    mel_camera_describe_free(&d);

    for (usize i = 0; i < g_app.modes.count; ++i)
    {
        Mel_Camera_Mode m = g_app.modes.items[i];
        char            text[96];
        snprintf(text, sizeof text, "%dx%d @%.0f %s", m.width, m.height, (f64)m.fps_max, mel_image_format_name(m.format));
        Mel_Gui_Handle btn = mel_button_create(g_app.modes_list, .text = str8_from_cstr(text), .pointer.on_click = on_mode_clicked, .user = (void*)(usize)i, .layoutable = { .preferred_h = BENCH_MODE_H });
        mel_array_push(&g_app.mode_btns, btn);
    }

    mel_gui_relayout(g_app.frame);
}

static void on_device_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    i32 idx = (i32)(usize)user;
    if (idx < 0 || (usize)idx >= g_app.devices.count)
        return;

    g_app.selected_dev = idx;

    Mel_Camera_Describe_Result d = mel_camera_describe(g_app.devices.items[idx], g_app.alloc);
    if (!mel_camera_status_failed(d.status))
        set_textf(g_app.device_label, "Selected: %.*s [%s]", (int)d.value.name.len, (const char*)d.value.name.data, mel_camera_facing_name(d.value.facing));
    else
        set_textf(g_app.device_label, "Selected: device %d", idx);
    mel_camera_describe_free(&d);

    rebuild_modes();
    update_buttons();
}

static void rebuild_devices(bool refresh)
{
    for (usize i = 0; i < g_app.device_btns.count; ++i)
        mel_gui_destroy(g_app.device_btns.items[i]);
    mel_array_clear(&g_app.device_btns);
    mel_array_clear(&g_app.devices);
    g_app.selected_dev = -1;
    clear_modes();

    if (refresh)
        mel_camera_refresh();
    u32 count = mel_camera_count();
    if (count > 0)
    {
        mel_array_reserve(&g_app.devices, count);
        g_app.devices.count = mel_camera_list(g_app.devices.items, count);
    }

    for (usize i = 0; i < g_app.devices.count; ++i)
    {
        char        name[96];
        const char* facing = "?";

        snprintf(name, sizeof name, "device %u", (u32)i);
        Mel_Camera_Describe_Result d = mel_camera_describe(g_app.devices.items[i], g_app.alloc);
        if (!mel_camera_status_failed(d.status))
        {
            i32 n = (i32)(d.value.name.len < 80 ? d.value.name.len : 80);
            memcpy(name, d.value.name.data, (usize)n);
            name[n] = 0;
            facing = mel_camera_facing_name(d.value.facing);
        }
        mel_camera_describe_free(&d);

        char text[160];
        snprintf(text, sizeof text, "%s  [%s]", name, facing);
        Mel_Gui_Handle btn = mel_button_create(g_app.devices_list, .text = str8_from_cstr(text), .pointer.on_click = on_device_clicked, .user = (void*)(usize)i, .layoutable = { .preferred_h = BENCH_BTN_H });
        mel_array_push(&g_app.device_btns, btn);
    }

    set_textf(g_app.device_label, "%u device(s), none selected", (u32)g_app.devices.count);
    mel_gui_relayout(g_app.frame);
    update_buttons();
}

static void push_hotplug_line(const char* line)
{
    for (i32 i = HOTPLUG_LINES - 1; i > 0; --i)
        memcpy(g_app.hotplug_text[i], g_app.hotplug_text[i - 1], sizeof g_app.hotplug_text[i]);
    snprintf(g_app.hotplug_text[0], sizeof g_app.hotplug_text[0], "%s", line);

    for (i32 i = 0; i < HOTPLUG_LINES; ++i)
        mel_gui_set_text(g_app.hotplug_labels[i], str8_from_cstr(g_app.hotplug_text[i]));
}

static void on_hotplug(const Mel_Camera_Event* ev, void* user)
{
    (void)user;
    const char* what = ev->added ? "added" : (ev->removed ? "removed" : "changed");
    char        line[128];

    g_app.hotplug_seq += 1;
    snprintf(line, sizeof line, "#%llu %s [%s]", (unsigned long long)g_app.hotplug_seq, what, mel_camera_facing_name(ev->facing));
    push_hotplug_line(line);
    mel_log_info(MEL_TAG, "hotplug: %s", line);

    if (ev->added || ev->removed)
        rebuild_devices(false);
}

static void close_stream(void)
{
    if (g_app.subscribed)
    {
        mel_camera_frame_unsubscribe(g_app.cam, g_app.sub);
        g_app.subscribed = false;
    }
    if (g_app.opened)
    {
        mel_camera_close(g_app.cam);
        g_app.opened = false;
    }
    g_app.started = false;

    mel_mutex_lock(&g_app.swap_lock);
    g_app.pending = false;
    mel_mutex_unlock(&g_app.swap_lock);
    g_app.preview_ready = false;
}

static void on_open_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (g_app.opened || g_app.selected_dev < 0 || g_app.selected_mode < 0)
        return;

    Mel_Camera        cam = g_app.devices.items[g_app.selected_dev];
    Mel_Camera_Mode   m = g_app.modes.items[g_app.selected_mode];
    Mel_Camera_Config cfg = { .format = m.format, .width = m.width, .height = m.height, .fps = m.fps_max };

    Mel_Future* f = mel_camera_open(cam, cfg, g_app.alloc);
    if (f == NULL || mel_camera_status_failed(mel_camera_future_status(f)))
    {
        mel_log_error(MEL_TAG, "open failed");
        mel_gui_set_text(g_app.state_label, S8("State: open FAILED"));
        if (f != NULL)
            mel_camera_future_free(f);
        return;
    }
    mel_camera_future_free(f);

    g_app.cam = cam;
    g_app.opened = true;
    g_app.sub = mel_camera_frame_subscribe(cam, on_frame, &g_app);
    g_app.subscribed = true;

    mel_mutex_lock(&g_app.swap_lock);
    g_app.stat_frames = 0;
    g_app.stat_seq = 0;
    mel_mutex_unlock(&g_app.swap_lock);
    g_app.fps_anchor_ns = 0;
    g_app.fps = 0.0;

    set_textf(g_app.state_label, "State: open %dx%d %s", m.width, m.height, mel_image_format_name(m.format));
    update_buttons();
}

static void on_start_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (!g_app.opened || g_app.started)
        return;

    Mel_Future* f = mel_camera_start(g_app.cam, g_app.alloc);
    if (f == NULL || mel_camera_status_failed(mel_camera_future_status(f)))
    {
        mel_log_error(MEL_TAG, "start failed");
        mel_gui_set_text(g_app.state_label, S8("State: start FAILED"));
        if (f != NULL)
            mel_camera_future_free(f);
        return;
    }
    mel_camera_future_free(f);

    g_app.started = true;
    mel_gui_set_text(g_app.state_label, S8("State: streaming"));
    update_buttons();
}

static void on_stop_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (!g_app.started)
        return;

    Mel_Future* f = mel_camera_stop(g_app.cam, g_app.alloc);
    if (f == NULL || mel_camera_status_failed(mel_camera_future_status(f)))
    {
        mel_log_error(MEL_TAG, "stop failed");
        mel_gui_set_text(g_app.state_label, S8("State: stop FAILED"));
        if (f != NULL)
            mel_camera_future_free(f);
        return;
    }
    mel_camera_future_free(f);

    g_app.started = false;
    mel_gui_set_text(g_app.state_label, S8("State: stopped (open)"));
    update_buttons();
}

static void on_close_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (!g_app.opened)
        return;

    close_stream();
    mel_gui_set_text(g_app.state_label, S8("State: closed"));
    mel_gui_invalidate(g_app.canvas);
    update_buttons();
}

static void on_mirror_toggled(Mel_Gui_Handle h, bool checked, void* user)
{
    (void)h;
    (void)user;
    mel_mutex_lock(&g_app.swap_lock);
    g_app.mirror = checked;
    mel_mutex_unlock(&g_app.swap_lock);
}

static void show_auth(const mel_camera_auth* a)
{
    g_app.authorized = mel_camera_auth_is_granted(a);
    set_textf(g_app.auth_label, "Authorization: %s", mel_camera_auth_name(a));
    update_buttons();
}

static void on_auth_resolved(Mel_Task* self)
{
    (void)self;
    const mel_camera_auth* a = mel_camera_future_auth(g_app.auth_future);
    mel_camera_future_free(g_app.auth_future);
    g_app.auth_future = NULL;

    mel_log_info(MEL_TAG, "authorization resolved: %s", mel_camera_auth_name(a));
    show_auth(a);
}

static void on_authorize_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (g_app.auth_future != NULL || g_app.authorized)
        return;

    g_app.auth_future = mel_camera_authorize(g_app.alloc);
    if (g_app.auth_future == NULL)
    {
        mel_log_error(MEL_TAG, "authorize returned no future");
        mel_gui_set_text(g_app.auth_label, S8("Authorization: unavailable"));
        return;
    }

    mel_gui_set_text(g_app.auth_label, S8("Authorization: requesting"));
    mel_task_init(&g_app.auth_task, on_auth_resolved);
    mel_future_then(g_app.auth_future, &g_app.auth_task, mel_vat_executor(g_app.vat));
    update_buttons();
}

static void on_refresh_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    rebuild_devices(true);
}

static void main_destroy(Mel_Gui_Handle frame, void* arg)
{
    (void)frame;
    (void)arg;

    close_stream();

    if (g_app.hotplug_subscribed)
    {
        mel_camera_unsubscribe(g_app.hotplug_sub);
        g_app.hotplug_subscribed = false;
    }

    mel_mutex_lock(&g_app.swap_lock);
    g_app.tearing_down = true;
    mel_mutex_unlock(&g_app.swap_lock);

    for (i32 i = 0; i < PREVIEW_BUFFERS; ++i)
    {
        if (g_app.preview[i].format != NULL)
            mel_image_free(&g_app.preview[i]);
    }
    if (g_app.raw_ready)
    {
        mel_image_free(&g_app.raw);
        g_app.raw_ready = false;
    }
    if (g_app.auth_future != NULL)
    {
        mel_camera_future_free(g_app.auth_future);
        g_app.auth_future = NULL;
    }

    mel_array_free(&g_app.device_btns);
    mel_array_free(&g_app.mode_btns);
    mel_array_free(&g_app.devices);
    mel_array_free(&g_app.modes);

    mel_mutex_destroy(&g_app.swap_lock);
    mel_camera_shutdown();
}

static void build_main(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    g_app.frame = frame;

    mel_gui_set_text(frame, S8("Camera Bench"));

    Mel_Layoutable side_spec;
    Mel_Layoutable canvas_spec;
#if BENCH_MOBILE
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 0, .margin = 0, .cross_align = MEL_ALIGN_STRETCH));
    side_spec = (Mel_Layoutable){ .weight = 1 };
    canvas_spec = (Mel_Layoutable){ .preferred_h = 320 };
#else
    mel_gui_set_bounds(frame, 60, 60, 1280, 1080);
    mel_gui_set_layout(frame, mel_row_layout(.spacing = 0, .margin = 0, .cross_align = MEL_ALIGN_STRETCH));
    side_spec = (Mel_Layoutable){ .fixed_w = 360 };
    canvas_spec = (Mel_Layoutable){ .preferred_w = 640, .preferred_h = 480, .weight = 1 };
#endif

    Mel_Gui_Handle side = mel_scrollview_create(frame, .layout = mel_column_layout(.spacing = 10, .margin = 10, .cross_align = MEL_ALIGN_STRETCH), .layoutable = side_spec);

    Mel_Gui_Handle auth_box = mel_groupbox_create(side, .title = S8("Authorization"), .layout = mel_column_layout(.spacing = 6, .margin = 8, .cross_align = MEL_ALIGN_STRETCH), .layoutable = { .preferred_h = BENCH_MOBILE ? 130 : 100 });
    g_app.auth_label = mel_label_create(auth_box, .text = S8("Authorization: unknown"), .layoutable = { .preferred_h = 20 });
    g_app.authorize_btn = mel_button_create(auth_box, .text = S8("Authorize"), .pointer.on_click = on_authorize_clicked, .layoutable = { .preferred_h = BENCH_BTN_H });

    g_app.devices_box = mel_groupbox_create(side, .title = S8("Devices"), .layout = mel_column_layout(.spacing = 6, .margin = 8, .cross_align = MEL_ALIGN_STRETCH), .layoutable = { .preferred_h = 240 });
    Mel_Gui_Handle refresh = mel_button_create(g_app.devices_box, .text = S8("Refresh"), .pointer.on_click = on_refresh_clicked, .layoutable = { .preferred_h = BENCH_BTN_H });
    (void)refresh;
    g_app.device_label = mel_label_create(g_app.devices_box, .text = S8("No devices listed"), .layoutable = { .preferred_h = 20 });
    g_app.devices_list = mel_scrollview_create(g_app.devices_box, .layout = mel_column_layout(.spacing = 4, .margin = 2, .cross_align = MEL_ALIGN_STRETCH), .layoutable = { .preferred_h = 120, .weight = 1 });

    g_app.modes_box = mel_groupbox_create(side, .title = S8("Modes"), .layout = mel_column_layout(.spacing = 4, .margin = 8, .cross_align = MEL_ALIGN_STRETCH), .layoutable = { .preferred_h = 280 });
    g_app.mode_label = mel_label_create(g_app.modes_box, .text = S8("No mode selected"), .layoutable = { .preferred_h = 20 });
    g_app.modes_list = mel_scrollview_create(g_app.modes_box, .layout = mel_column_layout(.spacing = 4, .margin = 2, .cross_align = MEL_ALIGN_STRETCH), .layoutable = { .preferred_h = 200, .weight = 1 });

    Mel_Gui_Handle stream_box = mel_groupbox_create(side, .title = S8("Stream"), .layout = mel_column_layout(.spacing = 6, .margin = 8, .cross_align = MEL_ALIGN_STRETCH), .layoutable = { .preferred_h = BENCH_MOBILE ? 190 : 140 });
    Mel_Gui_Handle row1 = mel_panel_create(stream_box, .layout = mel_row_layout(.spacing = 6, .cross_align = MEL_ALIGN_STRETCH), .layoutable = { .preferred_h = BENCH_BTN_H });
    g_app.open_btn = mel_button_create(row1, .text = S8("Open"), .disabled = true, .pointer.on_click = on_open_clicked, .layoutable = { .weight = 1 });
    g_app.start_btn = mel_button_create(row1, .text = S8("Start"), .disabled = true, .pointer.on_click = on_start_clicked, .layoutable = { .weight = 1 });
    Mel_Gui_Handle row2 = mel_panel_create(stream_box, .layout = mel_row_layout(.spacing = 6, .cross_align = MEL_ALIGN_STRETCH), .layoutable = { .preferred_h = BENCH_BTN_H });
    g_app.stop_btn = mel_button_create(row2, .text = S8("Stop"), .disabled = true, .pointer.on_click = on_stop_clicked, .layoutable = { .weight = 1 });
    g_app.close_btn = mel_button_create(row2, .text = S8("Close"), .disabled = true, .pointer.on_click = on_close_clicked, .layoutable = { .weight = 1 });
    g_app.mirror_check = mel_checkbox_create(stream_box, .text = S8("Mirror preview"), .on_.on_toggled = on_mirror_toggled, .layoutable = { .preferred_h = BENCH_CHK_H });

    Mel_Gui_Handle stats_box = mel_groupbox_create(side, .title = S8("Stats"), .layout = mel_column_layout(.spacing = 4, .margin = 8, .cross_align = MEL_ALIGN_STRETCH), .layoutable = { .preferred_h = 110 });
    g_app.state_label = mel_label_create(stats_box, .text = S8("State: closed"), .layoutable = { .preferred_h = 20 });
    g_app.frames_label = mel_label_create(stats_box, .text = S8("Frames: 0"), .layoutable = { .preferred_h = 20 });
    g_app.last_label = mel_label_create(stats_box, .text = S8("Last: -"), .layoutable = { .preferred_h = 20 });

    Mel_Gui_Handle hp_box = mel_groupbox_create(side, .title = S8("Hotplug"), .layout = mel_column_layout(.spacing = 4, .margin = 8, .cross_align = MEL_ALIGN_STRETCH), .layoutable = { .preferred_h = 130 });
    for (i32 i = 0; i < HOTPLUG_LINES; ++i)
        g_app.hotplug_labels[i] = mel_label_create(hp_box, .text = S8("-"), .layoutable = { .preferred_h = 18 });

    g_app.canvas = mel_canvas_create(frame, .w = 640, .h = 480, .on_.on_paint = on_paint, .user = &g_app, .layoutable = canvas_spec);

    show_auth(mel_camera_authorization());

    g_app.hotplug_sub = mel_camera_subscribe(mel_vat_executor(g_app.vat), on_hotplug, &g_app);
    g_app.hotplug_subscribed = true;

    rebuild_devices(true);
}

void mel_app_setup(Mel_Vat* root)
{
    mel_gui_init(root);

    g_app.alloc = mel_alloc_heap();
    g_app.vat = root;
    g_app.selected_dev = -1;
    g_app.selected_mode = -1;
    g_app.idx_produce = 0;
    g_app.idx_spare = 1;
    g_app.idx_consume = 2;
    mel_task_init(&g_app.gui_update_task, on_gui_update);
    mel_mutex_init(&g_app.swap_lock, MEL_MUTEX_PLAIN);

    mel_array_init(&g_app.device_btns, g_app.alloc);
    mel_array_init(&g_app.mode_btns, g_app.alloc);
    mel_array_init(&g_app.devices, g_app.alloc);
    mel_array_init(&g_app.modes, g_app.alloc);

    for (i32 i = 0; i < HOTPLUG_LINES; ++i)
        snprintf(g_app.hotplug_text[i], sizeof g_app.hotplug_text[i], "-");

    mel_camera_init(g_app.alloc, mel_vat_executor(root));

    mel_app_register_screen(S8("main"), .build = build_main, .on_destroy = main_destroy);
    mel_app_present(S8("main"), NULL);
}
