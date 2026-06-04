#include <test/test.h>
#include <sensor/sensor.h>
#include <sensor/events.h>
#include <sensor/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>
#include <collection.slotmap/slotmap.h>

#include <string.h>

#define FAKE_ID 0x1234ULL

static int g_fake_native_marker;

static struct
{
    bool            present;
    bool            streaming;
    Mel_Sensor_Sink sink;
    f32             accel_hz;
    f32             gyro_hz;
    f32             accel_max_override;
    bool            poll_only;
} g_fake;

static u32 fake_enumerate(void* user, Mel_Sensor_Raw* out, u32 cap)
{
    (void)user;
    if (cap == 0 || !g_fake.present)
        return 0;
    f32 accel_max = g_fake.accel_max_override > 0.0f ? g_fake.accel_max_override : 100.0f;
    out[0] = (Mel_Sensor_Raw){
        .stable_id = FAKE_ID,
        .name = S8("fake-imu"),
        .caps = {
            .has_accel = true,
            .has_gyro = true,
            .accel_min_hz = 10.0f,
            .accel_max_hz = accel_max,
            .gyro_min_hz = 10.0f,
            .gyro_max_hz = 100.0f,
            .side = MEL_SENSOR_SIDE_LEFT,
            .controller_id = 7,
        },
    };
    return 1;
}

static Mel_Sensor_Status fake_start(void* user, u64 id, const Mel_Sensor_Stream_Config* cfg, Mel_Sensor_Sink sink)
{
    (void)user;
    (void)id;
    g_fake.streaming = true;
    g_fake.sink = sink;
    g_fake.accel_hz = cfg->accel_hz;
    g_fake.gyro_hz = cfg->gyro_hz;
    if (g_fake.poll_only)
        return MEL_SENSOR_OK | MEL_SENSOR_WARNED | MEL_SENSOR_WARN_POLL_ONLY;
    return MEL_SENSOR_OK;
}

static void fake_stop(void* user, u64 id)
{
    (void)user;
    (void)id;
    g_fake.streaming = false;
}

static Mel_Sensor_Status fake_read(void* user, u64 id, Mel_Sensor_Reading* out)
{
    (void)user;
    (void)id;
    if (!g_fake.streaming)
        return MEL_SENSOR_OK | MEL_SENSOR_RESULT_NOT_STREAMING;
    *out = (Mel_Sensor_Reading){
        .timestamp_s = 1.5,
        .accel_mps2 = { 0.0f, 0.0f, 9.81f },
        .gyro_radps = { 0.1f, 0.2f, 0.3f },
        .valid_mask = MEL_SENSOR_VALID_ACCEL | MEL_SENSOR_VALID_GYRO,
    };
    return MEL_SENSOR_OK;
}

static void* fake_native(void* user, u64 id)
{
    (void)user;
    (void)id;
    return &g_fake_native_marker;
}

static Mel_Sensor_Provider register_fake(void)
{
    static const Mel_Sensor_Provider_Desc desc = {
        .name = "fake",
        .enumerate = fake_enumerate,
        .start = fake_start,
        .stop = fake_stop,
        .read = fake_read,
        .native = fake_native,
    };
    return mel_sensor_provider_register(&desc);
}

MEL_TEST(sensor, dead_handle_is_loud_not_fatal)
{
    Mel_Sensor bogus = { .h = { .index = 9999, .generation = 7 } };
    mel_sensor_init(mel_alloc_heap());
    MEL_EXPECT(!mel_sensor_alive(bogus));
    Mel_Sensor_Describe_Result r = mel_sensor_describe(bogus);
    MEL_EXPECT(mel_sensor_failed(r.status));
    mel_sensor_shutdown();
}

MEL_TEST(sensor, null_handle_is_dead)
{
    Mel_Sensor null = MEL_SENSOR_NULL;
    mel_sensor_init(mel_alloc_heap());
    MEL_EXPECT(!mel_sensor_alive(null));
    MEL_EXPECT(mel_sensor_equal(null, null));
    mel_sensor_shutdown();
}

MEL_TEST(sensor, provider_device_enumerates_and_describes)
{
    g_fake = (typeof(g_fake)){ .present = true };
    mel_sensor_init(mel_alloc_heap());
    register_fake();
    mel_sensor_refresh();

    MEL_REQUIRE_EQ(mel_sensor_count(), 1u);
    Mel_Sensor list[4];
    u32        n = mel_sensor_list(list, 4);
    MEL_REQUIRE_EQ(n, 1u);

    Mel_Sensor_Describe_Result d = mel_sensor_describe(list[0]);
    MEL_EXPECT(!mel_sensor_failed(d.status));
    MEL_EXPECT(d.value.caps.has_accel);
    MEL_EXPECT(d.value.caps.has_gyro);
    MEL_EXPECT_EQ(d.value.caps.side, MEL_SENSOR_SIDE_LEFT);
    MEL_EXPECT_EQ(d.value.caps.controller_id, 7u);

    mel_sensor_shutdown();
}

MEL_TEST(sensor, rate_query_returns_caps)
{
    g_fake = (typeof(g_fake)){ .present = true };
    mel_sensor_init(mel_alloc_heap());
    register_fake();
    mel_sensor_refresh();

    Mel_Sensor list[1];
    mel_sensor_list(list, 1);
    Mel_Sensor_Rate_Result r = mel_sensor_rates(list[0]);
    MEL_EXPECT(!mel_sensor_failed(r.status));
    MEL_EXPECT_FLOAT_EQ(r.value.accel_max_hz, 100.0f, 0.001f);
    MEL_EXPECT_FLOAT_EQ(r.value.gyro_min_hz, 10.0f, 0.001f);

    mel_sensor_shutdown();
}

MEL_TEST(sensor, start_clamps_rate_above_max)
{
    g_fake = (typeof(g_fake)){ .present = true };
    mel_sensor_init(mel_alloc_heap());
    register_fake();
    mel_sensor_refresh();

    Mel_Sensor list[1];
    mel_sensor_list(list, 1);
    Mel_Sensor_Status st = mel_sensor_start(list[0], .accel_hz = 1000.0f, .gyro_hz = 50.0f);
    MEL_EXPECT(mel_sensor_warned(st));
    MEL_EXPECT((st & MEL_SENSOR_WARN_RATE_CLAMPED) != 0u);
    MEL_EXPECT(mel_sensor_streaming(list[0]));
    MEL_EXPECT_FLOAT_EQ(g_fake.accel_hz, 100.0f, 0.001f);
    MEL_EXPECT_FLOAT_EQ(g_fake.gyro_hz, 50.0f, 0.001f);

    mel_sensor_shutdown();
}

MEL_TEST(sensor, read_before_stream_reports_not_streaming)
{
    g_fake = (typeof(g_fake)){ .present = true };
    mel_sensor_init(mel_alloc_heap());
    register_fake();
    mel_sensor_refresh();

    Mel_Sensor list[1];
    mel_sensor_list(list, 1);
    Mel_Sensor_Read_Result r = mel_sensor_read(list[0]);
    MEL_EXPECT(!mel_sensor_failed(r.status));
    MEL_EXPECT((r.status & MEL_SENSOR_RESULT_NOT_STREAMING) != 0u);

    mel_sensor_shutdown();
}

MEL_TEST(sensor, streamed_read_yields_units)
{
    g_fake = (typeof(g_fake)){ .present = true };
    mel_sensor_init(mel_alloc_heap());
    register_fake();
    mel_sensor_refresh();

    Mel_Sensor list[1];
    mel_sensor_list(list, 1);
    mel_sensor_start(list[0]);
    Mel_Sensor_Read_Result r = mel_sensor_read(list[0]);
    MEL_EXPECT(!mel_sensor_failed(r.status));
    MEL_EXPECT((r.value.valid_mask & MEL_SENSOR_VALID_ACCEL) != 0u);
    MEL_EXPECT((r.value.valid_mask & MEL_SENSOR_VALID_GYRO) != 0u);
    MEL_EXPECT_FLOAT_EQ(r.value.accel_mps2[2], 9.81f, 0.001f);
    MEL_EXPECT_GT(r.value.sequence, 0u);

    Mel_Sensor_Status sp = mel_sensor_stop(list[0]);
    MEL_EXPECT(!mel_sensor_failed(sp));
    MEL_EXPECT(!mel_sensor_streaming(list[0]));

    mel_sensor_shutdown();
}

typedef struct
{
    u32 added;
    u32 removed;
    u32 changed;
    u32 sample;
} Sink_Counts;

static void push_cb(const Mel_Sensor_Event* ev, void* user)
{
    Sink_Counts* c = (Sink_Counts*)user;
    switch (ev->kind)
    {
    case MEL_SENSOR_EVENT_ADDED:
        c->added++;
        break;
    case MEL_SENSOR_EVENT_REMOVED:
        c->removed++;
        break;
    case MEL_SENSOR_EVENT_CHANGED:
        c->changed++;
        break;
    case MEL_SENSOR_EVENT_SAMPLE:
        c->sample++;
        break;
    }
}

MEL_TEST(sensor, push_face_delivers_add_and_sample)
{
    g_fake = (typeof(g_fake)){ .present = true };
    mel_sensor_init_ex(mel_alloc_heap(), mel_executor_inline());
    register_fake();

    Sink_Counts             s = { 0 };
    Mel_Sensor_Subscription sub = mel_sensor_subscribe(mel_executor_inline(), push_cb, &s);
    MEL_REQUIRE(mel_slotmap_handle_valid(sub.handle));

    mel_sensor_refresh();
    MEL_EXPECT_EQ(s.added, 1u);

    Mel_Sensor list[1];
    mel_sensor_list(list, 1);
    mel_sensor_start(list[0]);

    Mel_Sensor_Reading r = { .timestamp_s = 2.0, .accel_mps2 = { 1, 2, 3 }, .valid_mask = MEL_SENSOR_VALID_ACCEL };
    g_fake.sink.on_sample(g_fake.sink.token, &r);
    MEL_EXPECT_EQ(s.sample, 1u);

    g_fake.present = false;
    mel_sensor_refresh();
    MEL_EXPECT_EQ(s.removed, 1u);

    mel_sensor_unsubscribe(sub);
    mel_sensor_shutdown();
}

MEL_TEST(sensor, pull_face_drains_hotplug_events)
{
    g_fake = (typeof(g_fake)){ .present = true };
    mel_sensor_init(mel_alloc_heap());
    register_fake();

    Mel_Sensor_Event drain[32];
    mel_sensor_poll_events(drain, 32);

    mel_sensor_refresh();
    u32 got = mel_sensor_poll_events(drain, 32);
    u32 added = 0;
    for (u32 i = 0; i < got; i++)
        if (drain[i].kind == MEL_SENSOR_EVENT_ADDED)
            added++;
    MEL_EXPECT_EQ(added, 1u);

    mel_sensor_shutdown();
}

MEL_TEST(sensor, caps_change_fires_changed_with_rates_field)
{
    g_fake = (typeof(g_fake)){ .present = true, .accel_max_override = 100.0f };
    mel_sensor_init(mel_alloc_heap());
    register_fake();
    mel_sensor_refresh();

    Mel_Sensor_Event drain[32];
    mel_sensor_poll_events(drain, 32);

    g_fake.accel_max_override = 200.0f;
    mel_sensor_refresh();

    u32 got = mel_sensor_poll_events(drain, 32);
    u32  changed = 0;
    bool rates = false;
    for (u32 i = 0; i < got; i++)
        if (drain[i].kind == MEL_SENSOR_EVENT_CHANGED)
        {
            changed++;
            if (drain[i].changed_fields & MEL_SENSOR_FIELD_RATES)
                rates = true;
        }
    MEL_EXPECT_EQ(changed, 1u);
    MEL_EXPECT(rates);

    mel_sensor_shutdown();
}

MEL_TEST(sensor, unregister_provider_drops_device_on_refresh)
{
    g_fake = (typeof(g_fake)){ .present = true };
    mel_sensor_init(mel_alloc_heap());
    Mel_Sensor_Provider p = register_fake();
    mel_sensor_refresh();
    MEL_REQUIRE_EQ(mel_sensor_count(), 1u);

    mel_sensor_provider_unregister(p);
    mel_sensor_refresh();
    MEL_EXPECT_EQ(mel_sensor_count(), 0u);

    mel_sensor_shutdown();
}

MEL_TEST(sensor, native_passthrough_returns_provider_pointer)
{
    g_fake = (typeof(g_fake)){ .present = true };
    mel_sensor_init(mel_alloc_heap());
    register_fake();
    mel_sensor_refresh();

    Mel_Sensor list[1];
    mel_sensor_list(list, 1);
    MEL_EXPECT(mel_sensor_native(list[0]) == &g_fake_native_marker);

    mel_sensor_shutdown();
}

MEL_TEST(sensor, poll_only_backend_warns_on_start)
{
    g_fake = (typeof(g_fake)){ .present = true, .poll_only = true };
    mel_sensor_init(mel_alloc_heap());
    register_fake();
    mel_sensor_refresh();

    Mel_Sensor list[1];
    mel_sensor_list(list, 1);
    Mel_Sensor_Status st = mel_sensor_start(list[0]);
    MEL_EXPECT(mel_sensor_warned(st));
    MEL_EXPECT((st & MEL_SENSOR_WARN_POLL_ONLY) != 0u);

    mel_sensor_shutdown();
}
