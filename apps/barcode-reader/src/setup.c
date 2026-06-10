#include <allocator/allocator.h>
#include <barcode/decode.h>
#include <boot/boot.h>
#include <camera/camera.h>
#include <collection/list.h>
#include <executor/executor.h>
#include <future/future.h>
#include <image/image.h>
#include <log/log.h>
#include <vat/vat.h>

#include <stdio.h>

static const char* MEL_TAG = "barcode-reader";

static void report(const mel_barcode_decode_result* r)
{
    if (r->found)
        mel_log_info(MEL_TAG, "decoded %s \"%.*s\" x=[%d,%d] y=%d", r->symbology, r->text_len, r->text, r->x_start, r->x_end, r->y);
    else
        mel_log_info(MEL_TAG, "no barcode found");
}

static int run_still(const char* path, const Mel_Alloc* a)
{
    mel_image_codec_init(a);

    Mel_Image img;
    if (!mel_image_load_file(&img, path, a))
    {
        mel_log_error(MEL_TAG, "failed to load image: %s", path);
        mel_image_codec_shutdown();
        return 1;
    }

    Mel_Image      gray_owned;
    bool           converted = false;
    mel_image_gray gray;
    if (mel_image_format_has_luma(img.format) && mel_image_format_channels(img.format) == 1)
    {
        gray = mel_image_gray_borrow(&img);
    }
    else if (mel_image_to_gray(&img, a, &gray_owned))
    {
        gray = mel_image_gray_borrow(&gray_owned);
        converted = true;
    }
    else
    {
        mel_log_error(MEL_TAG, "image has no luminance and conversion to gray failed");
        mel_image_free(&img);
        mel_image_codec_shutdown();
        return 1;
    }

    mel_barcode_decode_result r;
    bool                      ok = mel_barcode_decode(&gray, &r, a);
    report(&r);
    if (ok)
    {
        printf("%s\t%.*s\n", r.symbology, r.text_len, r.text);
        mel_barcode_decode_result_free(&r, a);
    }

    if (converted)
        mel_image_free(&gray_owned);
    mel_image_free(&img);
    mel_image_codec_shutdown();
    return ok ? 0 : 2;
}

typedef struct
{
    const Mel_Alloc*     alloc;
    Mel_Vat*             vat;
    mel_barcode_decoder  decoder;
    Mel_Camera           cam;
    Mel_Camera_Frame_Sub sub;
    bool                 decoder_ready;
    bool                 opened;
    bool                 subscribed;
    Mel_Task             step;
    Mel_Future*          pending;
} App;

static App g_app;

static void on_frame(const Mel_Camera_Frame* frame, void* user)
{
    App*           app = (App*)user;
    mel_image_gray gray = mel_image_gray_borrow(&frame->image);
    if (gray.pixels == NULL)
        return;

    mel_barcode_decode_result r;
    if (mel_barcode_decoder_decode(&app->decoder, &gray, &r))
    {
        mel_log_info(MEL_TAG, "frame %llu: %s \"%.*s\"", (unsigned long long)frame->sequence, r.symbology, r.text_len, r.text);
        mel_barcode_decode_result_free(&r, app->alloc);
    }
}

static void app_fail(App* app, const char* what)
{
    mel_log_error(MEL_TAG, "%s", what);
    mel_app_set_exit_code(1);
    mel_vat_quit(app->vat);
}

static void app_await(App* app, Mel_Future* f, void (*stage)(Mel_Task*), const char* what)
{
    if (f == NULL)
    {
        app_fail(app, what);
        return;
    }
    app->pending = f;
    mel_task_init(&app->step, stage);
    mel_future_then(f, &app->step, mel_vat_executor(app->vat));
}

static void on_started(Mel_Task* task)
{
    App*              app = mel_container_of(task, App, step);
    Mel_Camera_Status status = mel_camera_future_status(app->pending);
    mel_camera_future_free(app->pending);
    app->pending = NULL;
    if (mel_camera_status_failed(status))
    {
        app_fail(app, "camera start failed");
        return;
    }
    mel_log_info(MEL_TAG, "streaming; point a 1D barcode at the camera. ctrl-c to quit.");
}

static void on_opened(Mel_Task* task)
{
    App*              app = mel_container_of(task, App, step);
    Mel_Camera_Status status = mel_camera_future_status(app->pending);
    mel_camera_future_free(app->pending);
    app->pending = NULL;
    if (mel_camera_status_failed(status))
    {
        app_fail(app, "camera open failed");
        return;
    }
    app->opened = true;

    app->sub = mel_camera_frame_subscribe(app->cam, on_frame, app);
    app->subscribed = true;

    app_await(app, mel_camera_start(app->cam, app->alloc), on_started, "start returned no future");
}

static void begin_open(App* app)
{
    u32 count = mel_camera_count();
    mel_log_info(MEL_TAG, "cameras: %u", count);
    if (count == 0)
    {
        app_fail(app, "no camera devices; run \"barcode-reader <image-file>\" to decode a still image");
        return;
    }

    Mel_Camera cam = MEL_CAMERA_NULL;
    mel_camera_list(&cam, 1);

    Mel_Camera_Describe_Result d = mel_camera_describe(cam, app->alloc);
    if (mel_camera_status_failed(d.status) || d.value.modes.count == 0)
    {
        mel_camera_describe_free(&d);
        app_fail(app, "camera describe failed or has no modes");
        return;
    }
    Mel_Camera_Mode mode = d.value.modes.items[0];
    mel_log_info(MEL_TAG, "device \"%.*s\" mode %dx%d @ %.0ffps", (int)d.value.name.len, (const char*)d.value.name.data, mode.width, mode.height, (double)mode.fps_max);

    Mel_Camera_Config cfg = { .format = mode.format, .width = mode.width, .height = mode.height, .fps = mode.fps_max };
    i32               max_w = mode.width;
    mel_camera_describe_free(&d);

    if (!mel_barcode_decoder_init(&app->decoder, max_w, app->alloc))
    {
        app_fail(app, "decoder init failed");
        return;
    }
    app->decoder_ready = true;

    app->cam = cam;
    app_await(app, mel_camera_open(cam, cfg, app->alloc), on_opened, "open returned no future");
}

static void on_authorized(Mel_Task* task)
{
    App* app = mel_container_of(task, App, step);
    bool granted = mel_camera_future_auth(app->pending) == &mel_camera_auth_granted;
    mel_camera_future_free(app->pending);
    app->pending = NULL;
    if (!granted)
    {
        app_fail(app, "camera authorization denied");
        return;
    }
    begin_open(app);
}

static void app_start(App* app)
{
    mel_camera_init(app->alloc, mel_vat_executor(app->vat));

    if (mel_camera_authorization() == &mel_camera_auth_granted)
    {
        begin_open(app);
        return;
    }
    app_await(app, mel_camera_authorize(app->alloc), on_authorized, "authorize returned no future (camera uninitialized or OOM)");
}

static void app_teardown(void* user)
{
    App* app = (App*)user;
    if (app->subscribed)
    {
        mel_camera_frame_unsubscribe(app->cam, app->sub);
        app->subscribed = false;
    }
    if (app->opened)
    {
        mel_camera_close(app->cam);
        app->opened = false;
    }
    if (app->decoder_ready)
    {
        mel_barcode_decoder_free(&app->decoder);
        app->decoder_ready = false;
    }
    mel_camera_shutdown();
}

void mel_app_setup(Mel_Vat* root)
{
    if (mel_app_argc() >= 2)
    {
        mel_app_set_exit_code(run_still(mel_app_argv()[1], mel_vat_alloc(root)));
        return;
    }

    g_app.alloc = mel_vat_alloc(root);
    g_app.vat = root;
    mel_vat_retain(root);
    mel_app_on_exit(app_teardown, &g_app);
    app_start(&g_app);
}
