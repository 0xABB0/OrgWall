#include <camera/camera.h>
#include <camera/provider.h>
#include <test/test.h>

#include <image/image.h>
#include <image/format.h>
#include <image/convert.h>
#include <image/geometry.h>

#include <future/future.h>
#include <event/event.h>
#include <executor/executor.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <string.h>

#define MOCK_STABLE_ID 0xCA3EBA5Eull
#define MOCK_W         16
#define MOCK_H         8
#define MOCK_FRAMES    4

static u8 mock_y[MOCK_W * MOCK_H];
static u8 mock_uv[(MOCK_W / 2) * 2 * (MOCK_H / 2)];

static Mel_Camera_Sink mock_sink;
static bool            mock_have_sink;
static bool            mock_opened;
static u32             mock_frames_to_emit;
static bool            mock_auth_granted = true;

static u8 mock_sample(i32 x, i32 y) { return (u8)((x * 7 + y * 13 + 3) & 0xFF); }

static void mock_fill_y(void)
{
    for (i32 y = 0; y < MOCK_H; y++)
        for (i32 x = 0; x < MOCK_W; x++)
            mock_y[y * MOCK_W + x] = mock_sample(x, y);
    memset(mock_uv, 128, sizeof mock_uv);
}

static u32 mock_enumerate(void* user, Mel_Camera_Raw* out, u32 cap)
{
    (void)user;
    if (cap == 0)
        return 1;
    static const Mel_Camera_Mode modes[] = {
        { .format = &mel_image_nv12, .width = MOCK_W, .height = MOCK_H, .fps_min = 15.0f, .fps_max = 30.0f },
        { .format = &mel_image_nv12, .width = MOCK_W * 2, .height = MOCK_H * 2, .fps_min = 15.0f, .fps_max = 60.0f },
    };
    out[0].stable_id = MOCK_STABLE_ID;
    out[0].name = S8("mock-camera");
    out[0].facing = &mel_camera_back;
    out[0].modes = modes;
    out[0].mode_count = 2;
    return 1;
}

static bool mock_open(void* user, u64 stable_id, Mel_Camera_Config cfg, Mel_Camera_Sink sink)
{
    (void)user;
    (void)cfg;
    if (stable_id != MOCK_STABLE_ID)
        return false;
    mock_sink = sink;
    mock_have_sink = true;
    mock_opened = true;
    return true;
}

static void mock_close(void* user, u64 stable_id)
{
    (void)user;
    (void)stable_id;
    mock_have_sink = false;
    mock_opened = false;
}

static void mock_emit_frame(u64 ns)
{
    Mel_Image_Plane planes[2];
    planes[0] = (Mel_Image_Plane){ .pixels = mock_y, .stride = MOCK_W, .w = MOCK_W, .h = MOCK_H, .bpp = 1 };
    planes[1] = (Mel_Image_Plane){ .pixels = mock_uv, .stride = (MOCK_W / 2) * 2, .w = MOCK_W / 2, .h = MOCK_H / 2, .bpp = 2 };

    Mel_Image image;
    if (!mel_image_wrap(&image, &mel_image_nv12, MOCK_W, MOCK_H, planes, 2))
        return;

    Mel_Camera_Frame frame = {
        .image = image,
        .timestamp_ns = ns,
        .sequence = 0,
        .orient = { .quarter_turns = 0, .flip_x = false },
    };
    mock_sink.on_frame(mock_sink.token, &frame);
}

static Mel_Camera_Status mock_start(void* user, u64 stable_id)
{
    (void)user;
    (void)stable_id;
    if (!mock_have_sink)
        return MEL_CAMERA_ERROR;
    for (u32 i = 0; i < mock_frames_to_emit; i++)
        mock_emit_frame((u64)(i + 1) * 1000000ull);
    return MEL_CAMERA_OK;
}

static Mel_Camera_Status mock_stop(void* user, u64 stable_id)
{
    (void)user;
    (void)stable_id;
    return MEL_CAMERA_OK;
}

static const mel_camera_auth* mock_authorization(void* user)
{
    (void)user;
    return mock_auth_granted ? &mel_camera_auth_granted : &mel_camera_auth_denied;
}

static void mock_authorize(void* user, Mel_Camera_Sink sink)
{
    (void)user;
    if (sink.on_auth)
        sink.on_auth(sink.token, mock_auth_granted ? &mel_camera_auth_granted : &mel_camera_auth_denied);
}

static const Mel_Camera_Provider_Desc MOCK_DESC = {
    .name = "mock",
    .enumerate = mock_enumerate,
    .open = mock_open,
    .close = mock_close,
    .start = mock_start,
    .stop = mock_stop,
    .authorization = mock_authorization,
    .authorize = mock_authorize,
};

void mel_camera__register_host_providers(void) {}

static void install_mock(u32 frames)
{
    mock_have_sink = false;
    mock_opened = false;
    mock_frames_to_emit = frames;
    mock_auth_granted = true;
    mock_fill_y();
    mel_camera_init(mel_alloc_heap(), NULL);
    mel_camera_provider_register(&MOCK_DESC);
    mel_camera_refresh();
}

static Mel_Camera first_camera(void)
{
    Mel_Camera list[4];
    u32        n = mel_camera_list(list, 4);
    MEL_REQUIRE(n >= 1);
    return list[0];
}

MEL_TEST(camera, enumerate_sees_mock_device)
{
    install_mock(0);
    MEL_EXPECT_EQ(mel_camera_count(), (u32)1);
    Mel_Camera c = first_camera();
    MEL_EXPECT(mel_camera_alive(c));
    mel_camera_shutdown();
}

MEL_TEST(camera, describe_reports_facing_and_modes)
{
    install_mock(0);
    Mel_Camera                 c = first_camera();
    Mel_Camera_Describe_Result r = mel_camera_describe(c, mel_alloc_heap());
    MEL_EXPECT_EQ(r.status & MEL_CAMERA_SEVERITY_MASK, (Mel_Camera_Status)MEL_CAMERA_OK);
    MEL_EXPECT_EQ_STR8(r.value.name, S8("mock-camera"));
    MEL_EXPECT(r.value.facing == &mel_camera_back);
    MEL_EXPECT_EQ(r.value.modes.count, (usize)2);
    MEL_EXPECT_EQ(r.value.modes.items[0].width, (i32)MOCK_W);
    MEL_EXPECT(r.value.modes.items[0].format == &mel_image_nv12);
    mel_camera_describe_free(&r);
    mel_camera_shutdown();
}

MEL_TEST(camera, authorize_future_resolves_granted)
{
    install_mock(0);
    MEL_EXPECT(mel_camera_auth_is_granted(mel_camera_authorization()));

    Mel_Future* f = mel_camera_authorize(mel_alloc_heap());
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_future_resolved(f));
    const mel_camera_auth* a = mel_camera_future_auth(f);
    MEL_EXPECT(mel_camera_auth_is_granted(a));
    MEL_EXPECT(a == &mel_camera_auth_granted);
    mel_camera_future_free(f);
    mel_camera_shutdown();
}

MEL_TEST(camera, authorize_future_denied)
{
    install_mock(0);
    mock_auth_granted = false;
    Mel_Future* f = mel_camera_authorize(mel_alloc_heap());
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT(!mel_camera_auth_is_granted(mel_camera_future_auth(f)));
    MEL_EXPECT(mel_future_status_failed(mel_future_status(f)));
    mel_camera_future_free(f);
    mel_camera_shutdown();
}

MEL_TEST(camera, open_future_resolves)
{
    install_mock(0);
    Mel_Camera        c = first_camera();
    Mel_Camera_Config cfg = { .format = &mel_image_nv12, .width = MOCK_W, .height = MOCK_H, .fps = 30.0f };
    Mel_Future*       f = mel_camera_open(c, cfg, mel_alloc_heap());
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT_EQ(mel_camera_future_status(f) & MEL_CAMERA_SEVERITY_MASK, (Mel_Camera_Status)MEL_CAMERA_OK);
    MEL_EXPECT(mock_opened);
    mel_camera_future_free(f);
    mel_camera_close(c);
    mel_camera_shutdown();
}

MEL_TEST(camera, open_invalid_config_fails_loudly)
{
    install_mock(0);
    Mel_Camera        c = first_camera();
    Mel_Camera_Config cfg = { .format = NULL, .width = 0, .height = 0, .fps = 0.0f };
    Mel_Future*       f = mel_camera_open(c, cfg, mel_alloc_heap());
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_camera_status_failed(mel_camera_future_status(f)));
    mel_camera_future_free(f);
    mel_camera_shutdown();
}

typedef struct
{
    u32 received;
    u8  first_sample;
    i32 plane_count;
    u64 last_seq;
} Frame_Sink;

static void on_frame(const Mel_Camera_Frame* frame, void* user)
{
    Frame_Sink* fs = (Frame_Sink*)user;
    fs->plane_count = mel_image_plane_count(&frame->image);
    mel_image_gray g = mel_image_gray_borrow(&frame->image);
    fs->first_sample = g.pixels[0];
    fs->last_seq = frame->sequence;
    fs->received++;
}

MEL_TEST(camera, push_subscription_receives_each_frame)
{
    install_mock(MOCK_FRAMES);
    Mel_Camera        c = first_camera();
    Mel_Camera_Config cfg = { .format = &mel_image_nv12, .width = MOCK_W, .height = MOCK_H, .fps = 30.0f };
    mel_camera_future_free(mel_camera_open(c, cfg, mel_alloc_heap()));

    Frame_Sink           fs = { 0 };
    Mel_Camera_Frame_Sub sub = mel_camera_frame_subscribe(c, on_frame, &fs);
    MEL_REQUIRE(mel_slotmap_handle_valid(sub.handle));

    Mel_Future* sf = mel_camera_start(c, mel_alloc_heap());
    MEL_REQUIRE(sf != NULL);
    MEL_EXPECT(mel_future_resolved(sf));
    mel_camera_future_free(sf);

    MEL_EXPECT_EQ(fs.received, (u32)MOCK_FRAMES);
    MEL_EXPECT_EQ(fs.plane_count, (i32)2);
    MEL_EXPECT_EQ(fs.first_sample, mock_sample(0, 0));
    MEL_EXPECT_EQ(fs.last_seq, (u64)(MOCK_FRAMES - 1));

    mel_camera_frame_unsubscribe(c, sub);
    mel_camera_close(c);
    mel_camera_shutdown();
}

typedef struct
{
    bool ok;
    u8   sample;
} Convert_Result;

static void on_frame_convert(const Mel_Camera_Frame* frame, void* user)
{
    Convert_Result* cr = (Convert_Result*)user;
    Mel_Image       gray;
    if (!mel_image_to_gray(&frame->image, mel_alloc_heap(), &gray))
        return;
    Mel_Image_Plane p = mel_image_plane(&gray, 0);
    cr->sample = p.pixels[0];
    cr->ok = true;
    mel_image_free(&gray);
}

MEL_TEST(camera, frame_converts_to_gray8)
{
    install_mock(1);
    Mel_Camera        c = first_camera();
    Mel_Camera_Config cfg = { .format = &mel_image_nv12, .width = MOCK_W, .height = MOCK_H, .fps = 30.0f };
    mel_camera_future_free(mel_camera_open(c, cfg, mel_alloc_heap()));

    Convert_Result       cr = { 0 };
    Mel_Camera_Frame_Sub sub = mel_camera_frame_subscribe(c, on_frame_convert, &cr);
    mel_camera_future_free(mel_camera_start(c, mel_alloc_heap()));

    MEL_EXPECT(cr.ok);
    MEL_EXPECT_EQ(cr.sample, mock_sample(0, 0));

    mel_camera_frame_unsubscribe(c, sub);
    mel_camera_close(c);
    mel_camera_shutdown();
}

static u32 hotplug_seen;
static bool hotplug_added;

static void on_hotplug(const Mel_Camera_Event* ev, void* user)
{
    (void)user;
    hotplug_seen++;
    if (ev->added)
        hotplug_added = true;
}

MEL_TEST(camera, hotplug_event_fires_on_add_and_remove)
{
    hotplug_seen = 0;
    hotplug_added = false;
    mock_have_sink = false;
    mock_opened = false;
    mock_frames_to_emit = 0;
    mock_fill_y();
    mel_camera_init(mel_alloc_heap(), NULL);
    Mel_Camera_Hotplug_Sub sub = mel_camera_subscribe(mel_executor_inline(), on_hotplug, NULL);

    mel_camera_provider_register(&MOCK_DESC);
    mel_camera_refresh();
    MEL_EXPECT(hotplug_added);
    MEL_EXPECT_GE(hotplug_seen, (u32)1);

    mel_camera_unsubscribe(sub);
    mel_camera_shutdown();
}

MEL_TEST(camera, stop_close_teardown_and_unsubscribe)
{
    install_mock(2);
    Mel_Camera        c = first_camera();
    Mel_Camera_Config cfg = { .format = &mel_image_nv12, .width = MOCK_W, .height = MOCK_H, .fps = 30.0f };
    mel_camera_future_free(mel_camera_open(c, cfg, mel_alloc_heap()));

    Frame_Sink           fs = { 0 };
    Mel_Camera_Frame_Sub sub = mel_camera_frame_subscribe(c, on_frame, &fs);
    mel_camera_future_free(mel_camera_start(c, mel_alloc_heap()));
    MEL_EXPECT_EQ(fs.received, (u32)2);

    Mel_Future* stp = mel_camera_stop(c, mel_alloc_heap());
    MEL_REQUIRE(stp != NULL);
    MEL_EXPECT(mel_future_resolved(stp));
    MEL_EXPECT_EQ(mel_camera_future_status(stp) & MEL_CAMERA_SEVERITY_MASK, (Mel_Camera_Status)MEL_CAMERA_OK);
    mel_camera_future_free(stp);

    mel_camera_frame_unsubscribe(c, sub);
    mel_camera_close(c);
    MEL_EXPECT(!mock_opened);
    mel_camera_shutdown();
}

MEL_TEST(camera, pull_subscription_on_frames_is_rejected)
{
    install_mock(0);
    Mel_Camera        c = first_camera();
    Mel_Camera_Config cfg = { .format = &mel_image_nv12, .width = MOCK_W, .height = MOCK_H, .fps = 30.0f };
    mel_camera_future_free(mel_camera_open(c, cfg, mel_alloc_heap()));

    Mel_Camera_Frame frame;
    MEL_EXPECT(!mel_camera_frame_pull(c, &frame));

    mel_camera_close(c);
    mel_camera_shutdown();
}
