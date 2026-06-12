#include <test/test.h>

#include <audioplayback/audioplayback.h>
#include <audioout/audioout.h>
#include <audioout/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <allocator/tracking.h>
#include <core/types.h>
#include <string/str8.h>

#include <string.h>

void mel_audioout__register_host_providers(void) {}

typedef struct
{
    u32                   grant_rate;
    u32                   grant_channels;
    u32                   grant_block;
    bool                  grant_exclusive;
    bool                  grant_os_timestamps;
    u32                   grant_latency;
    bool                  busy;
    bool                  unsupported;
    bool                  opened;
    bool                  started;
    u32                   closes;
    Mel_AudioOut_Open_Opt seen_opt;
    Mel_AudioOut_Source   src;
} Mock_State;

static Mock_State mock;

static str8 cstr(const char* s) { return (str8){ (u8*)s, (size)strlen(s) }; }

static void mock_enumerate(void* user, Mel_AudioOut_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    u32              rates[1] = { 48000 };
    Mel_AudioOut_Raw raw = {
        .stable_id = cstr("mock:out"),
        .name = cstr("Mock Out"),
        .kind = &mel_audioout_builtin,
        .channels = 2,
        .samplerate = 48000,
        .samplerates = rates,
        .samplerate_count = 1,
    };
    fn(&raw, fn_user);
}

static str8 mock_default_id(void* user)
{
    MEL_UNUSED(user);
    return cstr("mock:out");
}

static Mel_AudioOut_Status mock_open(void* user, str8 stable_id, Mel_AudioOut_Format req, Mel_AudioOut_Open_Opt opt, Mel_AudioOut_Granted* granted, Mel_AudioOut_Source src)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(req);
    if (mock.busy)
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_BUSY;
    if (mock.unsupported)
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    mock.seen_opt = opt;
    mock.src = src;
    mock.opened = true;
    granted->format.samplerate = mock.grant_rate;
    granted->format.channels = mock.grant_channels;
    granted->format.block_frames = mock.grant_block;
    granted->exclusive = opt.exclusive && mock.grant_exclusive;
    granted->os_timestamps = mock.grant_os_timestamps;
    granted->latency_frames = mock.grant_latency;
    return MEL_AUDIOOUT_OK;
}

static void mock_start(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    mock.started = true;
}

static void mock_stop(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    mock.started = false;
}

static void mock_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    mock.closes++;
    mock.opened = false;
}

static const Mel_AudioOut_Provider_Desc MOCK_DESC = {
    .name = "mock",
    .enumerate = mock_enumerate,
    .default_id = mock_default_id,
    .open = mock_open,
    .start = mock_start,
    .stop = mock_stop,
    .close = mock_close,
};

static Mel_AudioOut install(void)
{
    memset(&mock, 0, sizeof mock);
    mock.grant_rate = 48000;
    mock.grant_channels = 2;
    mock.grant_block = 128;
    mel_audioout_init(mel_alloc_heap(), NULL);
    mel_audioout_provider_register(&MOCK_DESC);
    mel_audioout_refresh();
    return mel_audioout_find(S8("mock:out"));
}

static u32 device_pull(f32* dst, u32 frames) { return mock.src.pull(mock.src.token, dst, frames); }

MEL_TEST(audioplayback, open_gating_dead_busy_unsupported)
{
    Mel_AudioOut dev = install();

    Mel_AudioPlayback_Opt opt = { .sample_rate = 48000, .channels = 2, .ring_capacity_frames = 256 };

    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(mel_alloc_heap(), MEL_AUDIOOUT_NULL, opt);
    MEL_EXPECT(mel_audioplayback_status_failed(r.status));
    MEL_EXPECT(r.status & MEL_AUDIOPLAYBACK_RESULT_NO_DEVICE);

    mock.busy = true;
    r = mel_audioplayback_open(mel_alloc_heap(), dev, opt);
    MEL_EXPECT(mel_audioplayback_status_failed(r.status));
    MEL_EXPECT(r.status & MEL_AUDIOPLAYBACK_RESULT_BUSY);
    mock.busy = false;

    mock.unsupported = true;
    r = mel_audioplayback_open(mel_alloc_heap(), dev, opt);
    MEL_EXPECT(mel_audioplayback_status_failed(r.status));
    MEL_EXPECT(r.status & MEL_AUDIOPLAYBACK_RESULT_UNSUPPORTED);
    mock.unsupported = false;

    mel_audioout_shutdown();
}

MEL_TEST(audioplayback, native_write_passthrough)
{
    Mel_AudioOut dev = install();

    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(mel_alloc_heap(),
                                                             dev,
                                                             (Mel_AudioPlayback_Opt){
                                                                 .sample_rate = 48000,
                                                                 .channels = 2,
                                                                 .ring_capacity_frames = 256,
                                                             });
    MEL_REQUIRE(!mel_audioplayback_status_failed(r.status));
    MEL_EXPECT_EQ(r.status & ~MEL_AUDIOPLAYBACK_SEVERITY_MASK, 0u);
    MEL_EXPECT(mock.opened);
    MEL_EXPECT(mock.started);

    f32 src[32 * 2];
    for (u32 i = 0; i < 32; i++)
    {
        src[i * 2 + 0] = (f32)i;
        src[i * 2 + 1] = -(f32)i;
    }
    MEL_EXPECT_EQ(mel_audioplayback_write(r.playback, src, 32), 32u);

    f32 dst[32 * 2];
    MEL_EXPECT_EQ(device_pull(dst, 32), 32u);
    MEL_EXPECT_FLOAT_EQ(dst[0], 0.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[10 * 2], 10.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[10 * 2 + 1], -10.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[31 * 2], 31.0f, 0.0f);

    mel_audioplayback_close(r.playback);
    MEL_EXPECT_EQ(mock.closes, 1u);
    mel_audioout_shutdown();
}

MEL_TEST(audioplayback, write_underrun_pads_and_counts)
{
    Mel_AudioOut dev = install();

    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(mel_alloc_heap(),
                                                             dev,
                                                             (Mel_AudioPlayback_Opt){
                                                                 .sample_rate = 48000,
                                                                 .channels = 2,
                                                                 .ring_capacity_frames = 256,
                                                             });
    MEL_REQUIRE(!mel_audioplayback_status_failed(r.status));

    f32 src[16 * 2];
    for (u32 i = 0; i < 16 * 2; i++)
        src[i] = 1.0f;
    MEL_EXPECT_EQ(mel_audioplayback_write(r.playback, src, 16), 16u);

    f32 dst[64 * 2];
    MEL_EXPECT_EQ(device_pull(dst, 64), 64u);
    MEL_EXPECT_FLOAT_EQ(dst[15 * 2], 1.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[16 * 2], 0.0f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[63 * 2 + 1], 0.0f, 0.0f);
    MEL_EXPECT_EQ(mel_audioplayback_underrun_frames(r.playback), 48u);
    MEL_EXPECT(mel_audioplayback_status(r.playback) & MEL_AUDIOPLAYBACK_WARN_UNDERRUN);

    MEL_EXPECT_EQ(mel_audioplayback_write(r.playback, src, 16), 16u);
    MEL_EXPECT(!(mel_audioplayback_status(r.playback) & MEL_AUDIOPLAYBACK_WARN_UNDERRUN));
    MEL_EXPECT_EQ(mel_audioplayback_underrun_frames(r.playback), 48u);

    mel_audioplayback_close(r.playback);
    mel_audioout_shutdown();
}

typedef struct
{
    u32 calls;
    u32 limit;
    f32 fill;
} Pull_Source;

static u32 caller_pull(void* user, f32* dst, u32 frames)
{
    Pull_Source* ps = user;
    u32          give = frames < ps->limit ? frames : ps->limit;
    for (u32 i = 0; i < give * 2u; i++)
        dst[i] = ps->fill;
    ps->calls++;
    return give;
}

MEL_TEST(audioplayback, pull_mode_direct_delivery)
{
    Mel_AudioOut dev = install();

    Pull_Source                   ps = { .limit = 24, .fill = 0.5f };
    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(mel_alloc_heap(),
                                                             dev,
                                                             (Mel_AudioPlayback_Opt){
                                                                 .sample_rate = 48000,
                                                                 .channels = 2,
                                                                 .pull = caller_pull,
                                                                 .user = &ps,
                                                             });
    MEL_REQUIRE(!mel_audioplayback_status_failed(r.status));

    f32 dst[32 * 2];
    MEL_EXPECT_EQ(device_pull(dst, 32), 24u);
    MEL_EXPECT_EQ(ps.calls, 1u);
    MEL_EXPECT_FLOAT_EQ(dst[0], 0.5f, 0.0f);
    MEL_EXPECT_FLOAT_EQ(dst[23 * 2 + 1], 0.5f, 0.0f);
    MEL_EXPECT_EQ(mel_audioplayback_underrun_frames(r.playback), 0u);
    MEL_EXPECT(!(mel_audioplayback_status(r.playback) & MEL_AUDIOPLAYBACK_WARN_UNDERRUN));

    mel_audioplayback_close(r.playback);
    mel_audioout_shutdown();
}

MEL_TEST(audioplayback, converted_open_warns_and_resamples)
{
    Mel_AudioOut dev = install();

    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(mel_alloc_heap(),
                                                             dev,
                                                             (Mel_AudioPlayback_Opt){
                                                                 .sample_rate = 24000,
                                                                 .channels = 1,
                                                                 .ring_capacity_frames = 256,
                                                             });
    MEL_REQUIRE(!mel_audioplayback_status_failed(r.status));
    MEL_EXPECT(mel_audioplayback_status_warned(r.status));
    MEL_EXPECT(r.status & MEL_AUDIOPLAYBACK_WARN_CONVERTED);

    f32 ramp[64];
    for (u32 i = 0; i < 64; i++)
        ramp[i] = (f32)i;
    MEL_EXPECT_EQ(mel_audioplayback_write(r.playback, ramp, 64), 64u);

    f32 dst[32 * 2];
    MEL_EXPECT_EQ(device_pull(dst, 32), 32u);

    for (u32 k = 1; k < 32; k++)
    {
        MEL_EXPECT(dst[k * 2] > dst[(k - 1) * 2]);
        MEL_EXPECT_FLOAT_EQ(dst[k * 2] - dst[(k - 1) * 2], 0.5f, 1e-4f);
        MEL_EXPECT_FLOAT_EQ(dst[k * 2 + 1], dst[k * 2], 0.0f);
    }

    f32 last = dst[31 * 2];
    MEL_EXPECT_EQ(device_pull(dst, 32), 32u);
    MEL_EXPECT_FLOAT_EQ(dst[0] - last, 0.5f, 1e-4f);

    mel_audioplayback_close(r.playback);
    mel_audioout_shutdown();
}

MEL_TEST(audioplayback, exclusive_honesty)
{
    Mel_AudioOut dev = install();

    mock.grant_exclusive = false;
    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(mel_alloc_heap(),
                                                             dev,
                                                             (Mel_AudioPlayback_Opt){
                                                                 .sample_rate = 48000,
                                                                 .channels = 2,
                                                                 .ring_capacity_frames = 256,
                                                                 .exclusive = true,
                                                             });
    MEL_REQUIRE(!mel_audioplayback_status_failed(r.status));
    MEL_EXPECT(r.status & MEL_AUDIOPLAYBACK_WARN_EXCLUSIVE_DROPPED);
    MEL_EXPECT(mock.seen_opt.exclusive);
    MEL_EXPECT(!mel_audioplayback_granted(r.playback).exclusive);
    mel_audioplayback_close(r.playback);

    mock.grant_exclusive = true;
    r = mel_audioplayback_open(mel_alloc_heap(),
                               dev,
                               (Mel_AudioPlayback_Opt){
                                   .sample_rate = 48000,
                                   .channels = 2,
                                   .ring_capacity_frames = 256,
                                   .exclusive = true,
                               });
    MEL_REQUIRE(!mel_audioplayback_status_failed(r.status));
    MEL_EXPECT(!(r.status & MEL_AUDIOPLAYBACK_WARN_EXCLUSIVE_DROPPED));
    MEL_EXPECT(mel_audioplayback_granted(r.playback).exclusive);
    mel_audioplayback_close(r.playback);

    mel_audioout_shutdown();
}

MEL_TEST(audioplayback, latency_reports_device_plus_ring)
{
    Mel_AudioOut dev = install();
    mock.grant_latency = 100;
    mock.grant_os_timestamps = true;

    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(mel_alloc_heap(),
                                                             dev,
                                                             (Mel_AudioPlayback_Opt){
                                                                 .sample_rate = 48000,
                                                                 .channels = 2,
                                                                 .ring_capacity_frames = 256,
                                                             });
    MEL_REQUIRE(!mel_audioplayback_status_failed(r.status));
    MEL_EXPECT(mel_audioplayback_granted(r.playback).os_timestamps);
    MEL_EXPECT_EQ(mel_audioplayback_latency_frames(r.playback), 100u);

    f32 src[32 * 2];
    memset(src, 0, sizeof src);
    MEL_EXPECT_EQ(mel_audioplayback_write(r.playback, src, 32), 32u);
    MEL_EXPECT_EQ(mel_audioplayback_latency_frames(r.playback), 132u);

    mel_audioplayback_close(r.playback);
    mel_audioout_shutdown();
}

MEL_TEST(audioplayback, latency_scales_to_stream_rate)
{
    Mel_AudioOut dev = install();
    mock.grant_latency = 96;

    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(mel_alloc_heap(),
                                                             dev,
                                                             (Mel_AudioPlayback_Opt){
                                                                 .sample_rate = 24000,
                                                                 .channels = 2,
                                                                 .ring_capacity_frames = 256,
                                                             });
    MEL_REQUIRE(!mel_audioplayback_status_failed(r.status));
    MEL_EXPECT_EQ(mel_audioplayback_latency_frames(r.playback), 48u);

    mel_audioplayback_close(r.playback);
    mel_audioout_shutdown();
}

MEL_TEST(audioplayback, lost_is_sticky_and_rejects_writes)
{
    Mel_AudioOut dev = install();

    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(mel_alloc_heap(),
                                                             dev,
                                                             (Mel_AudioPlayback_Opt){
                                                                 .sample_rate = 48000,
                                                                 .channels = 2,
                                                                 .ring_capacity_frames = 256,
                                                             });
    MEL_REQUIRE(!mel_audioplayback_status_failed(r.status));

    mock.src.on_lost(mock.src.token);

    Mel_AudioPlayback_Status st = mel_audioplayback_status(r.playback);
    MEL_EXPECT(mel_audioplayback_status_failed(st));
    MEL_EXPECT(st & MEL_AUDIOPLAYBACK_RESULT_LOST);

    f32 src[8 * 2];
    memset(src, 0, sizeof src);
    MEL_EXPECT_EQ(mel_audioplayback_write(r.playback, src, 8), 0u);

    mel_audioplayback_close(r.playback);
    mel_audioout_shutdown();
}

MEL_TEST(audioplayback, allocator_round_trip)
{
    Mel_AudioOut dev = install();

    Mel_Track_Allocator track;
    mel_track_init(&track, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap() });
    Mel_Alloc track_alloc = mel_track_allocator(&track);

    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(&track_alloc,
                                                             dev,
                                                             (Mel_AudioPlayback_Opt){
                                                                 .sample_rate = 24000,
                                                                 .channels = 1,
                                                                 .ring_capacity_frames = 256,
                                                             });
    MEL_REQUIRE(!mel_audioplayback_status_failed(r.status));

    f32 ramp[64];
    for (u32 i = 0; i < 64; i++)
        ramp[i] = (f32)i;
    MEL_EXPECT_EQ(mel_audioplayback_write(r.playback, ramp, 64), 64u);
    f32 dst[32 * 2];
    MEL_EXPECT_EQ(device_pull(dst, 32), 32u);

    mel_audioplayback_close(r.playback);

    Mel_Track_Allocator_Stats s = mel_track_stats(&track);
    MEL_EXPECT_EQ(s.live_allocs, (usize)0);
    MEL_EXPECT_EQ(s.live_bytes, (usize)0);

    mel_track_shutdown(&track);
    mel_audioout_shutdown();
}
