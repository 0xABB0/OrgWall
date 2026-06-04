#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <barcode/decode.h>
#include <camera/camera.h>
#include <image/image.h>
#include <log/log.h>
#include <reactor/reactor.h>

#include <stdio.h>

static const char* MEL_TAG = "barcode-reader";

static void report(const mel_barcode_decode_result* r)
{
    if (r->found)
        mel_log_info(MEL_TAG, "decoded %s \"%.*s\" x=[%d,%d] y=%d", r->symbology, r->text_len, r->text, r->x_start, r->x_end, r->y);
    else
        mel_log_info(MEL_TAG, "no barcode found");
}

static int run_still(const char* path)
{
    const Mel_Alloc* a = mel_alloc_heap();
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
    mel_barcode_decoder  decoder;
    Mel_Camera           cam;
    Mel_Camera_Frame_Sub sub;
    bool                 opened;
    bool                 subscribed;
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

static bool app_authorize(const Mel_Alloc* a)
{
    if (mel_camera_authorization() == &mel_camera_auth_granted)
        return true;

    Mel_Future* af = mel_camera_authorize(a);
    if (af == NULL)
    {
        mel_log_error(MEL_TAG, "authorize returned no future (camera uninitialized or OOM)");
        return false;
    }
    bool granted = mel_camera_future_auth(af) == &mel_camera_auth_granted;
    mel_camera_future_free(af);
    if (!granted)
        mel_log_error(MEL_TAG, "camera authorization denied");
    return granted;
}

static bool app_init(Mel_Reactor* reactor, void* user)
{
    (void)user;
    const Mel_Alloc* a = mel_alloc_heap();
    g_app.alloc = a;
    mel_camera_init(a, reactor);

    if (!app_authorize(a))
    {
        mel_reactor_quit(reactor);
        return true;
    }

    u32 count = mel_camera_count();
    mel_log_info(MEL_TAG, "cameras: %u", count);
    if (count == 0)
    {
        mel_log_error(MEL_TAG, "no camera devices; run \"barcode-reader <image-file>\" to decode a still image");
        mel_reactor_quit(reactor);
        return true;
    }

    Mel_Camera cam = MEL_CAMERA_NULL;
    mel_camera_list(&cam, 1);

    Mel_Camera_Describe_Result d = mel_camera_describe(cam, a);
    if (mel_camera_status_failed(d.status) || d.value.modes.count == 0)
    {
        mel_log_error(MEL_TAG, "camera describe failed or has no modes");
        mel_camera_describe_free(&d);
        mel_reactor_quit(reactor);
        return true;
    }
    Mel_Camera_Mode mode = d.value.modes.items[0];
    mel_log_info(MEL_TAG, "device \"%.*s\" mode %dx%d @ %.0ffps", (int)d.value.name.len, (const char*)d.value.name.data, mode.width, mode.height, (double)mode.fps_max);

    Mel_Camera_Config cfg = { .format = mode.format, .width = mode.width, .height = mode.height, .fps = mode.fps_max };
    i32               max_w = mode.width;
    mel_camera_describe_free(&d);

    if (!mel_barcode_decoder_init(&g_app.decoder, max_w, a))
    {
        mel_log_error(MEL_TAG, "decoder init failed");
        mel_reactor_quit(reactor);
        return true;
    }

    Mel_Future* of = mel_camera_open(cam, cfg, a);
    if (of == NULL)
    {
        mel_log_error(MEL_TAG, "open returned no future (camera uninitialized or OOM)");
        mel_barcode_decoder_free(&g_app.decoder);
        mel_reactor_quit(reactor);
        return true;
    }
    if (mel_camera_status_failed(mel_camera_future_status(of)))
    {
        mel_log_error(MEL_TAG, "open failed status=0x%x", mel_camera_future_status(of));
        mel_camera_future_free(of);
        mel_barcode_decoder_free(&g_app.decoder);
        mel_reactor_quit(reactor);
        return true;
    }
    mel_camera_future_free(of);
    g_app.cam = cam;
    g_app.opened = true;

    g_app.sub = mel_camera_frame_subscribe(cam, on_frame, &g_app);
    g_app.subscribed = true;

    Mel_Future* sf = mel_camera_start(cam, a);
    bool        start_ok = sf != NULL && !mel_camera_status_failed(mel_camera_future_status(sf));
    if (!start_ok)
    {
        if (sf == NULL)
            mel_log_error(MEL_TAG, "start returned no future (camera uninitialized or OOM)");
        else
            mel_log_error(MEL_TAG, "start failed status=0x%x", mel_camera_future_status(sf));
        if (sf)
            mel_camera_future_free(sf);
        mel_camera_frame_unsubscribe(cam, g_app.sub);
        g_app.subscribed = false;
        mel_camera_close(cam);
        g_app.opened = false;
        mel_barcode_decoder_free(&g_app.decoder);
        mel_reactor_quit(reactor);
        return true;
    }
    mel_camera_future_free(sf);

    mel_log_info(MEL_TAG, "streaming; point a 1D barcode at the camera. ctrl-c to quit.");
    return true;
}

static void app_teardown(void)
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
    mel_barcode_decoder_free(&g_app.decoder);
    mel_camera_shutdown();
}

int main(int argc, char** argv)
{
    if (argc >= 2)
        return run_still(argv[1]);
    int rc = mel_reactor_spawn(MEL_REACTOR_THREADED, app_init, NULL);
    app_teardown();
    return rc;
}
