#include <test/test.h>

#include <audiocapture/audiocapture.h>

#include <audioin/audioin.h>
#include <audioin/permission.h>
#include <audioin/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/types.h>
#include <string/str8.h>

#include <string.h>

void mel_audioin__register_host_providers(void) {}

typedef struct
{
    const mel_audioin_auth* auth;
    Mel_AudioIn_Sink        sink;
    bool                    opened;
    Mel_AudioIn_Open_Opt    last_opt;
    Mel_AudioIn_Granted     grant;
    Mel_AudioIn_Status      open_status;
    u32                     rates[1];
} Mock_State;

static Mock_State mock;

static void mock_enumerate(void* user, Mel_AudioIn_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    Mel_AudioIn_Raw raw = {
        .stable_id = S8("mock:mic"),
        .name = S8("Mock Mic"),
        .kind = &mel_audioin_builtin,
        .channels = 2,
        .samplerate = 48000,
        .samplerates = mock.rates,
        .samplerate_count = 1,
        .caps = { .gain = false },
    };
    fn(&raw, fn_user);
}

static str8 mock_default_id(void* user)
{
    MEL_UNUSED(user);
    return S8("mock:mic");
}

static Mel_AudioIn_Status mock_open(void* user, str8 stable_id, Mel_AudioIn_Sink sink, Mel_AudioIn_Open_Opt opt, Mel_AudioIn_Granted* granted)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    if (mel_audioin_status_failed(mock.open_status))
        return mock.open_status;
    mock.sink = sink;
    mock.opened = true;
    mock.last_opt = opt;
    *granted = mock.grant;
    return MEL_AUDIOIN_OK;
}

static void mock_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    mock.opened = false;
}

static const mel_audioin_auth* mock_authorization(void* user)
{
    MEL_UNUSED(user);
    return mock.auth;
}

static const Mel_AudioIn_Provider_Desc MOCK_DESC = {
    .name = "mock",
    .enumerate = mock_enumerate,
    .default_id = mock_default_id,
    .open = mock_open,
    .close = mock_close,
    .authorization = mock_authorization,
};

static Mel_AudioIn install(void)
{
    memset(&mock, 0, sizeof mock);
    mock.auth = &mel_audioin_auth_granted;
    mock.rates[0] = 48000;
    mock.open_status = MEL_AUDIOIN_OK;
    mel_audioin_init(mel_alloc_heap(), NULL);
    mel_audioin_provider_register(&MOCK_DESC);
    mel_audioin_refresh();
    return mel_audioin_find(S8("mock:mic"));
}

static void push(const f32* interleaved, u32 frames, u32 rate, u32 channels, u64 ts) { mock.sink.on_frames(mock.sink.token, interleaved, frames, rate, channels, ts); }

MEL_TEST(audiocapture, open_requires_consent)
{
    Mel_AudioIn dev = install();
    mock.auth = &mel_audioin_auth_denied;

    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(mel_alloc_heap(), dev, (Mel_AudioCapture_Opt){ .sample_rate = 48000, .channels = 2, .ring_capacity_frames = 256 });
    MEL_EXPECT(mel_audiocapture_status_failed(r.status));
    MEL_EXPECT(r.status & MEL_AUDIOCAPTURE_RESULT_DENIED);
    MEL_EXPECT_NULL(r.capture);

    mel_audioin_shutdown();
}

MEL_TEST(audiocapture, open_dead_handle_is_no_device)
{
    install();
    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(mel_alloc_heap(), MEL_AUDIOIN_NULL, (Mel_AudioCapture_Opt){ .sample_rate = 48000, .channels = 2, .ring_capacity_frames = 256 });
    MEL_EXPECT(mel_audiocapture_status_failed(r.status));
    MEL_EXPECT(r.status & MEL_AUDIOCAPTURE_RESULT_NO_DEVICE);
    mel_audioin_shutdown();
}

MEL_TEST(audiocapture, open_busy_passes_through)
{
    Mel_AudioIn dev = install();
    mock.open_status = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_BUSY;

    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(mel_alloc_heap(), dev, (Mel_AudioCapture_Opt){ .sample_rate = 48000, .channels = 2, .ring_capacity_frames = 256 });
    MEL_EXPECT(mel_audiocapture_status_failed(r.status));
    MEL_EXPECT(r.status & MEL_AUDIOCAPTURE_RESULT_BUSY);

    mel_audioin_shutdown();
}

MEL_TEST(audiocapture, native_format_passthrough)
{
    Mel_AudioIn dev = install();

    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(mel_alloc_heap(), dev, (Mel_AudioCapture_Opt){ .sample_rate = 48000, .channels = 2, .ring_capacity_frames = 256 });
    MEL_REQUIRE(!mel_audiocapture_status_failed(r.status));
    MEL_EXPECT(!(r.status & MEL_AUDIOCAPTURE_WARN_CONVERTED));
    MEL_REQUIRE(mock.opened);

    f32 in[32 * 2];
    for (u32 i = 0; i < 32u; i++)
    {
        in[i * 2 + 0] = (f32)i;
        in[i * 2 + 1] = -(f32)i;
    }
    push(in, 32, 48000, 2, 0);

    MEL_EXPECT_EQ(mel_audiocapture_available(r.capture), 32u);
    f32 out[32 * 2];
    MEL_EXPECT_EQ(mel_audiocapture_read(r.capture, out, 32), 32u);
    for (u32 i = 0; i < 32u * 2u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i], in[i], 0.0f);

    mel_audiocapture_close(r.capture);
    MEL_EXPECT(!mock.opened);
    mel_audioin_shutdown();
}

MEL_TEST(audiocapture, downmix_to_mono)
{
    Mel_AudioIn dev = install();

    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(mel_alloc_heap(), dev, (Mel_AudioCapture_Opt){ .sample_rate = 48000, .channels = 1, .ring_capacity_frames = 256 });
    MEL_REQUIRE(!mel_audiocapture_status_failed(r.status));
    MEL_EXPECT(r.status & MEL_AUDIOCAPTURE_WARN_CONVERTED);

    f32 in[16 * 2];
    for (u32 i = 0; i < 16u; i++)
    {
        in[i * 2 + 0] = 2.0f * (f32)i;
        in[i * 2 + 1] = 4.0f * (f32)i;
    }
    push(in, 16, 48000, 2, 0);

    f32 out[16];
    MEL_EXPECT_EQ(mel_audiocapture_read(r.capture, out, 16), 16u);
    for (u32 i = 0; i < 16u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i], 3.0f * (f32)i, 1e-5f);

    mel_audiocapture_close(r.capture);
    mel_audioin_shutdown();
}

MEL_TEST(audiocapture, resamples_across_batches)
{
    Mel_AudioIn dev = install();

    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(mel_alloc_heap(), dev, (Mel_AudioCapture_Opt){ .sample_rate = 48000, .channels = 1, .ring_capacity_frames = 1024 });
    MEL_REQUIRE(!mel_audiocapture_status_failed(r.status));

    f32 batch[50];
    for (u32 b = 0; b < 4u; b++)
    {
        for (u32 i = 0; i < 50u; i++)
            batch[i] = (f32)(b * 50u + i);
        push(batch, 50, 24000, 1, 0);
    }

    u32 total = mel_audiocapture_available(r.capture);
    MEL_EXPECT_GE(total, 395u);
    MEL_EXPECT_LE(total, 400u);

    f32 out[400];
    u32 got = mel_audiocapture_read(r.capture, out, 400);
    MEL_EXPECT_EQ(got, total);
    for (u32 i = 0; i + 2u < got; i++)
    {
        f32 expect = (f32)i * 0.5f;
        MEL_EXPECT_FLOAT_EQ(out[i], expect, 0.51f);
    }
    for (u32 i = 1; i < got; i++)
        MEL_EXPECT_GE(out[i], out[i - 1]);

    mel_audiocapture_close(r.capture);
    mel_audioin_shutdown();
}

MEL_TEST(audiocapture, processing_exclusive_honesty)
{
    Mel_AudioIn dev = install();
    mock.grant = (Mel_AudioIn_Granted){ .processing = { .noise_suppression = true }, .exclusive = false, .os_timestamps = true };

    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(mel_alloc_heap(),
                                                           dev,
                                                           (Mel_AudioCapture_Opt){
                                                               .sample_rate = 48000,
                                                               .channels = 2,
                                                               .ring_capacity_frames = 256,
                                                               .exclusive = true,
                                                               .processing = { .echo_cancellation = true, .noise_suppression = true },
                                                           });
    MEL_REQUIRE(!mel_audiocapture_status_failed(r.status));
    MEL_EXPECT(mel_audiocapture_status_warned(r.status));
    MEL_EXPECT(r.status & MEL_AUDIOCAPTURE_WARN_PROCESSING_DROPPED);
    MEL_EXPECT(r.status & MEL_AUDIOCAPTURE_WARN_EXCLUSIVE_DROPPED);

    MEL_EXPECT(mock.last_opt.processing.echo_cancellation);
    MEL_EXPECT(mock.last_opt.exclusive);

    Mel_AudioCapture_Granted g = mel_audiocapture_granted(r.capture);
    MEL_EXPECT(!g.processing.echo_cancellation);
    MEL_EXPECT(g.processing.noise_suppression);
    MEL_EXPECT(!g.exclusive);
    MEL_EXPECT(g.os_timestamps);

    mel_audiocapture_close(r.capture);
    mel_audioin_shutdown();
}

MEL_TEST(audiocapture, overrun_counts_and_clears_on_read)
{
    Mel_AudioIn dev = install();

    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(mel_alloc_heap(), dev, (Mel_AudioCapture_Opt){ .sample_rate = 48000, .channels = 2, .ring_capacity_frames = 16 });
    MEL_REQUIRE(!mel_audiocapture_status_failed(r.status));

    f32 in[32 * 2] = { 0 };
    push(in, 32, 48000, 2, 0);

    MEL_EXPECT_EQ(mel_audiocapture_dropped_frames(r.capture), 16ull);
    Mel_AudioCapture_Status st = mel_audiocapture_status(r.capture);
    MEL_EXPECT(mel_audiocapture_status_warned(st));
    MEL_EXPECT(st & MEL_AUDIOCAPTURE_WARN_OVERRUN);

    f32 out[16 * 2];
    MEL_EXPECT_EQ(mel_audiocapture_read(r.capture, out, 16), 16u);
    st = mel_audiocapture_status(r.capture);
    MEL_EXPECT(!(st & MEL_AUDIOCAPTURE_WARN_OVERRUN));
    MEL_EXPECT_EQ(mel_audiocapture_dropped_frames(r.capture), 16ull);

    mel_audiocapture_close(r.capture);
    mel_audioin_shutdown();
}

MEL_TEST(audiocapture, sticky_lost_drains_then_zero)
{
    Mel_AudioIn dev = install();

    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(mel_alloc_heap(), dev, (Mel_AudioCapture_Opt){ .sample_rate = 48000, .channels = 2, .ring_capacity_frames = 64 });
    MEL_REQUIRE(!mel_audiocapture_status_failed(r.status));

    f32 in[10 * 2] = { 0 };
    push(in, 10, 48000, 2, 0);
    mock.sink.on_lost(mock.sink.token);

    Mel_AudioCapture_Status st = mel_audiocapture_status(r.capture);
    MEL_EXPECT(mel_audiocapture_status_failed(st));
    MEL_EXPECT(st & MEL_AUDIOCAPTURE_RESULT_LOST);

    f32 out[16 * 2];
    MEL_EXPECT_EQ(mel_audiocapture_read(r.capture, out, 16), 10u);
    MEL_EXPECT_EQ(mel_audiocapture_read(r.capture, out, 16), 0u);

    st = mel_audiocapture_status(r.capture);
    MEL_EXPECT(st & MEL_AUDIOCAPTURE_RESULT_LOST);

    mel_audiocapture_close(r.capture);
    mel_audioin_shutdown();
}

MEL_TEST(audiocapture, timestamps_advance_with_reads)
{
    Mel_AudioIn dev = install();
    mock.grant.os_timestamps = true;

    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(mel_alloc_heap(), dev, (Mel_AudioCapture_Opt){ .sample_rate = 48000, .channels = 1, .ring_capacity_frames = 256 });
    MEL_REQUIRE(!mel_audiocapture_status_failed(r.status));

    const u64 base = 1000000000ull;
    const u64 per_frame = (u64)(1e9 / 48000.0);

    f32 in[48] = { 0 };
    push(in, 48, 48000, 1, base);
    push(in, 48, 48000, 1, base + 48ull * per_frame);

    f32                   out[48];
    Mel_AudioCapture_Read got = mel_audiocapture_read_ex(r.capture, out, 24);
    MEL_EXPECT_EQ(got.frames, 24u);
    MEL_EXPECT_EQ(got.timestamp_ns, base);

    got = mel_audiocapture_read_ex(r.capture, out, 48);
    MEL_EXPECT_EQ(got.frames, 48u);
    u64 expect = base + 24ull * per_frame;
    MEL_EXPECT_LE(got.timestamp_ns > expect ? got.timestamp_ns - expect : expect - got.timestamp_ns, 1000ull);

    mel_audiocapture_close(r.capture);
    mel_audioin_shutdown();
}

MEL_TEST(audiocapture, zero_timestamp_falls_back_to_arrival)
{
    Mel_AudioIn dev = install();

    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(mel_alloc_heap(), dev, (Mel_AudioCapture_Opt){ .sample_rate = 48000, .channels = 1, .ring_capacity_frames = 64 });
    MEL_REQUIRE(!mel_audiocapture_status_failed(r.status));
    MEL_EXPECT(!mel_audiocapture_granted(r.capture).os_timestamps);

    f32 in[8] = { 0 };
    push(in, 8, 48000, 1, 0);

    f32                   out[8];
    Mel_AudioCapture_Read got = mel_audiocapture_read_ex(r.capture, out, 8);
    MEL_EXPECT_EQ(got.frames, 8u);
    MEL_EXPECT_GT(got.timestamp_ns, 0ull);

    mel_audiocapture_close(r.capture);
    mel_audioin_shutdown();
}
