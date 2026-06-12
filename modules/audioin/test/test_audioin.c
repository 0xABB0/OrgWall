#include <test/test.h>

#include <audioin/audioin.h>
#include <audioin/events.h>
#include <audioin/permission.h>
#include <audioin/os.h>
#include <audioin/provider.h>

#include "../src/audioin_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/types.h>
#include <collection/array.h>
#include <future/future.h>
#include <executor/executor.h>
#include <string/str8.h>

#include <string.h>

void mel_audioin__register_host_providers(void) {}

typedef struct
{
    const char* stable_id;
    const char* name;
    u32         channels;
    u32         samplerate;
    bool        gain_cap;
} Mock_Device;

typedef struct
{
    Mock_Device             devices[8];
    u32                     count;
    u32                     default_idx;
    u32                     rates[2];
    f32                     gain_value;
    bool                    set_gain_called;
    const mel_audioin_auth* auth;
    bool                    defer_authorize;
    Mel_AudioIn_Sink        deferred_sink;
    bool                    has_deferred;
} Mock_State;

static Mock_State mock1;
static Mock_State mock2;

static str8 cstr(const char* s) { return (str8){ (u8*)s, (size)strlen(s) }; }

static void mock_enumerate(void* user, Mel_AudioIn_Enum_Fn fn, void* fn_user)
{
    Mock_State* m = user;
    for (u32 i = 0; i < m->count; i++)
    {
        Mel_AudioIn_Raw raw = {
            .stable_id = cstr(m->devices[i].stable_id),
            .name = cstr(m->devices[i].name),
            .kind = &mel_audioin_builtin,
            .channels = m->devices[i].channels,
            .samplerate = m->devices[i].samplerate,
            .samplerates = m->rates,
            .samplerate_count = 2,
            .caps = { .gain = m->devices[i].gain_cap },
        };
        if (!fn(&raw, fn_user))
            return;
    }
}

static str8 mock_default_id(void* user)
{
    Mock_State* m = user;
    if (m->default_idx >= m->count)
        return STR8_EMPTY;
    return cstr(m->devices[m->default_idx].stable_id);
}

static f32 mock_gain(void* user, str8 stable_id)
{
    MEL_UNUSED(stable_id);
    Mock_State* m = user;
    return m->gain_value;
}

static Mel_AudioIn_Status mock_set_gain(void* user, str8 stable_id, f32 gain)
{
    MEL_UNUSED(stable_id);
    Mock_State* m = user;
    m->gain_value = gain;
    m->set_gain_called = true;
    return MEL_AUDIOIN_OK;
}

static const mel_audioin_auth* mock_authorization(void* user)
{
    Mock_State* m = user;
    return m->auth;
}

static void mock_authorize(void* user, Mel_AudioIn_Sink sink)
{
    Mock_State* m = user;
    if (m->defer_authorize)
    {
        m->deferred_sink = sink;
        m->has_deferred = true;
        return;
    }
    if (sink.on_auth)
        sink.on_auth(sink.token, m->auth);
}

static const Mel_AudioIn_Provider_Desc MOCK_DESC1 = {
    .name = "mock1",
    .user = &mock1,
    .enumerate = mock_enumerate,
    .default_id = mock_default_id,
    .gain = mock_gain,
    .set_gain = mock_set_gain,
    .authorization = mock_authorization,
    .authorize = mock_authorize,
};

static const Mel_AudioIn_Provider_Desc MOCK_DESC2 = {
    .name = "mock2",
    .user = &mock2,
    .enumerate = mock_enumerate,
    .default_id = mock_default_id,
    .authorization = mock_authorization,
};

static void mock_reset(Mock_State* m)
{
    memset(m, 0, sizeof *m);
    m->rates[0] = 44100;
    m->rates[1] = 48000;
    m->gain_value = 0.5f;
    m->auth = &mel_audioin_auth_granted;
    m->default_idx = 0;
}

static Mel_AudioIn_Provider install(void)
{
    mock_reset(&mock1);
    mock1.devices[0] = (Mock_Device){ "mock:alpha", "Alpha Mic", 1, 48000, true };
    mock1.devices[1] = (Mock_Device){ "mock:beta", "Beta Mic", 2, 44100, false };
    mock1.count = 2;
    mel_audioin_init(mel_alloc_heap(), NULL);
    Mel_AudioIn_Provider p = mel_audioin_provider_register(&MOCK_DESC1);
    mel_audioin_refresh();
    return p;
}

MEL_TEST(audioin, enumerates_and_describes)
{
    install();
    MEL_EXPECT_EQ(mel_audioin_count(), 2u);

    Mel_AudioIn alpha = mel_audioin_find(S8("mock:alpha"));
    MEL_REQUIRE(mel_audioin_alive(alpha));

    Mel_AudioIn_Describe_Result d = mel_audioin_describe(alpha, mel_alloc_heap());
    MEL_EXPECT(!mel_audioin_status_failed(d.status));
    MEL_EXPECT_EQ_STR8(d.value.name, S8("Alpha Mic"));
    MEL_EXPECT_EQ_STR8(d.value.stable_id, S8("mock:alpha"));
    MEL_EXPECT(d.value.kind == &mel_audioin_builtin);
    MEL_EXPECT_EQ(d.value.channels, 1u);
    MEL_EXPECT_EQ(d.value.samplerate, 48000u);
    MEL_EXPECT_EQ(d.value.samplerates.count, (usize)2);
    MEL_EXPECT_EQ(d.value.samplerates.items[1], 48000u);
    MEL_EXPECT(d.value.caps.gain);
    mel_audioin_describe_free(&d);

    mel_audioin_shutdown();
}

MEL_TEST(audioin, find_unknown_is_null)
{
    install();
    Mel_AudioIn ghost = mel_audioin_find(S8("mock:ghost"));
    MEL_EXPECT(!mel_audioin_alive(ghost));
    MEL_EXPECT(mel_audioin_equal(ghost, MEL_AUDIOIN_NULL));
    mel_audioin_shutdown();
}

MEL_TEST(audioin, reconciliation_keeps_surviving_handles)
{
    install();
    Mel_AudioIn alpha = mel_audioin_find(S8("mock:alpha"));
    Mel_AudioIn beta = mel_audioin_find(S8("mock:beta"));
    MEL_REQUIRE(mel_audioin_alive(alpha));
    MEL_REQUIRE(mel_audioin_alive(beta));

    mock1.devices[1] = (Mock_Device){ "mock:gamma", "Gamma Mic", 2, 96000, false };
    mel_audioin_refresh();

    MEL_EXPECT_EQ(mel_audioin_count(), 2u);
    MEL_EXPECT(mel_audioin_alive(alpha));
    MEL_EXPECT(!mel_audioin_alive(beta));
    MEL_EXPECT(mel_audioin_equal(mel_audioin_find(S8("mock:alpha")), alpha));
    MEL_EXPECT(mel_audioin_alive(mel_audioin_find(S8("mock:gamma"))));

    mel_audioin_shutdown();
}

MEL_TEST(audioin, default_tracks_provider)
{
    install();
    Mel_AudioIn alpha = mel_audioin_find(S8("mock:alpha"));
    MEL_EXPECT(mel_audioin_equal(mel_audioin_default(), alpha));

    mock1.default_idx = 1;
    mel_audioin_refresh();
    Mel_AudioIn beta = mel_audioin_find(S8("mock:beta"));
    MEL_EXPECT(mel_audioin_equal(mel_audioin_default(), beta));

    mel_audioin_shutdown();
}

typedef struct
{
    u32         added;
    u32         removed;
    u32         changed;
    u32         default_changed;
    Mel_AudioIn last_added;
    Mel_AudioIn last_removed;
} Event_Tally;

static void on_event(const Mel_AudioIn_Event* ev, void* user)
{
    Event_Tally* t = user;
    if (ev->added)
    {
        t->added++;
        t->last_added = ev->device;
    }
    if (ev->removed)
    {
        t->removed++;
        t->last_removed = ev->device;
    }
    if (ev->changed)
        t->changed++;
    if (ev->default_changed)
        t->default_changed++;
}

MEL_TEST(audioin, hotplug_event_payloads)
{
    Mel_AudioIn_Provider p = install();
    Event_Tally          tally = { 0 };

    Mel_AudioIn_Hotplug_Sub sub = mel_audioin_subscribe(NULL, on_event, &tally);
    MEL_REQUIRE(mel_slotmap_handle_valid(sub.handle));

    mock1.devices[2] = (Mock_Device){ "mock:delta", "Delta Mic", 1, 16000, false };
    mock1.count = 3;
    mel_audioin_provider_notify(p);
    MEL_EXPECT_EQ(tally.added, 1u);
    MEL_EXPECT(mel_audioin_equal(tally.last_added, mel_audioin_find(S8("mock:delta"))));

    Mel_AudioIn beta = mel_audioin_find(S8("mock:beta"));
    mock1.devices[1] = mock1.devices[2];
    mock1.count = 2;
    mel_audioin_provider_notify(p);
    MEL_EXPECT_EQ(tally.removed, 1u);
    MEL_EXPECT(mel_audioin_equal(tally.last_removed, beta));

    mock1.devices[0].name = "Alpha Mic Pro";
    mel_audioin_provider_notify(p);
    MEL_EXPECT_EQ(tally.changed, 1u);

    mock1.default_idx = 1;
    mel_audioin_provider_notify(p);
    MEL_EXPECT_EQ(tally.default_changed, 1u);

    mel_audioin_unsubscribe(sub);
    mel_audioin_shutdown();
}

MEL_TEST(audioin, refresh_without_change_is_silent)
{
    install();
    Event_Tally             tally = { 0 };
    Mel_AudioIn_Hotplug_Sub sub = mel_audioin_subscribe(NULL, on_event, &tally);

    mel_audioin_refresh();
    MEL_EXPECT_EQ(tally.added, 0u);
    MEL_EXPECT_EQ(tally.removed, 0u);
    MEL_EXPECT_EQ(tally.changed, 0u);
    MEL_EXPECT_EQ(tally.default_changed, 0u);

    mel_audioin_unsubscribe(sub);
    mel_audioin_shutdown();
}

MEL_TEST(audioin, authorize_future_grant_and_deny)
{
    install();
    MEL_EXPECT(mel_audioin_auth_is_granted(mel_audioin_authorization()));

    Mel_Future* f = mel_audioin_authorize(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(f);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT(mel_audioin_auth_is_granted(mel_audioin_future_auth(f)));
    mel_audioin_future_free(f);

    mock1.auth = &mel_audioin_auth_denied;
    MEL_EXPECT(!mel_audioin_auth_is_granted(mel_audioin_authorization()));

    f = mel_audioin_authorize(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(f);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT(mel_audioin_future_auth(f) == &mel_audioin_auth_denied);
    MEL_EXPECT(mel_future_status_failed(mel_future_status(f)));
    mel_audioin_future_free(f);

    mel_audioin_shutdown();
}

MEL_TEST(audioin, authorize_future_deferred_resolution)
{
    install();
    mock1.defer_authorize = true;

    Mel_Future* f = mel_audioin_authorize(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(f);
    MEL_EXPECT(!mel_future_resolved(f));
    MEL_REQUIRE(mock1.has_deferred);

    mock1.deferred_sink.on_auth(mock1.deferred_sink.token, mock1.auth);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT(mel_audioin_auth_is_granted(mel_audioin_future_auth(f)));
    mel_audioin_future_free(f);

    mel_audioin_shutdown();
}

MEL_TEST(audioin, authorization_is_most_restrictive)
{
    install();
    mock_reset(&mock2);
    mock2.devices[0] = (Mock_Device){ "mock2:omega", "Omega Feed", 1, 48000, false };
    mock2.count = 1;
    mock2.auth = &mel_audioin_auth_restricted;
    mel_audioin_provider_register(&MOCK_DESC2);
    mel_audioin_refresh();

    MEL_EXPECT(mel_audioin_authorization() == &mel_audioin_auth_restricted);

    mock1.auth = &mel_audioin_auth_denied;
    MEL_EXPECT(mel_audioin_authorization() == &mel_audioin_auth_denied);

    mel_audioin_shutdown();
}

MEL_TEST(audioin, gain_caps_gating)
{
    install();
    Mel_AudioIn alpha = mel_audioin_find(S8("mock:alpha"));
    Mel_AudioIn beta = mel_audioin_find(S8("mock:beta"));

    Mel_AudioIn_Status st = mel_audioin_set_gain(beta, 0.75f);
    MEL_EXPECT(mel_audioin_status_failed(st));
    MEL_EXPECT(st & MEL_AUDIOIN_RESULT_UNSUPPORTED);
    MEL_EXPECT(!mock1.set_gain_called);

    st = mel_audioin_set_gain(alpha, 0.75f);
    MEL_EXPECT(!mel_audioin_status_failed(st));
    MEL_EXPECT(mock1.set_gain_called);
    MEL_EXPECT_FLOAT_EQ(mel_audioin_gain(alpha), 0.75f, 0.0f);

    mel_audioin_shutdown();
}

MEL_TEST(audioin, multi_provider_unified_listing)
{
    install();
    mock_reset(&mock2);
    mock2.devices[0] = (Mock_Device){ "mock2:omega", "Omega Feed", 1, 48000, false };
    mock2.count = 1;
    mel_audioin_provider_register(&MOCK_DESC2);
    mel_audioin_refresh();

    MEL_EXPECT_EQ(mel_audioin_count(), 3u);
    MEL_EXPECT(mel_audioin_alive(mel_audioin_find(S8("mock:alpha"))));
    MEL_EXPECT(mel_audioin_alive(mel_audioin_find(S8("mock2:omega"))));

    Mel_AudioIn list[8];
    MEL_EXPECT_EQ(mel_audioin_list(list, 8), 3u);

    mel_audioin_shutdown();
}

typedef struct
{
    u32 frames;
    u32 samplerate;
    u32 channels;
    f32 first_sample;
    u32 lost;
    int id;
} Consume_State;

static void consume_on_frames(void* token, const f32* interleaved, u32 frames, u32 samplerate, u32 channels, u64 timestamp_ns)
{
    MEL_UNUSED(timestamp_ns);
    Consume_State* c = token;
    if (c->frames == 0 && frames > 0)
        c->first_sample = interleaved[0];
    c->frames += frames;
    c->samplerate = samplerate;
    c->channels = channels;
}

static void consume_on_lost(void* token)
{
    Consume_State* c = token;
    c->lost++;
}

MEL_TEST(audioin, publish_lifecycle)
{
    install();
    const Mel_Alloc* a = mel_alloc_heap();
    Event_Tally      tally = { 0 };
    mel_audioin_subscribe(NULL, on_event, &tally);

    u32 before = mel_audioin_count();

    Mel_AudioIn_Publish_Result pub = mel_audioin_publish(a,
                                                         (Mel_AudioIn_Publish_Opt){
                                                             .name = S8("App Feed"),
                                                             .channels = 2,
                                                             .samplerate = 48000,
                                                             .ring_capacity_frames = 1024,
                                                         });
    MEL_REQUIRE(!mel_audioin_status_failed(pub.status));
    MEL_EXPECT(mel_audioin_status_warned(pub.status));
    MEL_EXPECT(pub.status & MEL_AUDIOIN_WARN_LOCAL_ONLY);
    MEL_EXPECT(!mel_audioin_publish_os_visible(pub.published));
    MEL_EXPECT_EQ(mel_audioin_count(), before + 1u);
    MEL_EXPECT_EQ(tally.added, 1u);
    MEL_REQUIRE(mel_audioin_alive(pub.device));

    Mel_AudioIn_Describe_Result d = mel_audioin_describe(pub.device, a);
    MEL_EXPECT(d.value.kind == &mel_audioin_virtual);
    MEL_EXPECT_EQ(d.value.channels, 2u);
    MEL_EXPECT(str8_starts_with(d.value.stable_id, S8("publish:")));
    MEL_EXPECT(mel_audioin_equal(mel_audioin_find(d.value.stable_id), pub.device));
    mel_audioin_describe_free(&d);

    f32 block[64 * 2];
    for (u32 i = 0; i < 64u * 2u; i++)
        block[i] = 0.25f;
    MEL_EXPECT_EQ(mel_audioin_publish_feed(pub.published, block, 64u), 64u);

    Consume_State       consumer = { 0 };
    Mel_AudioIn_Sink    sink = { .on_frames = consume_on_frames, .on_lost = consume_on_lost, .token = &consumer };
    Mel_AudioIn_Granted granted = { 0 };
    Mel_AudioIn_Open_Opt want = { .processing = { .echo_cancellation = true }, .exclusive = true };
    MEL_EXPECT(!mel_audioin_status_failed(mel_audioin__open(pub.device, sink, want, &granted)));
    MEL_EXPECT(!granted.processing.echo_cancellation);
    MEL_EXPECT(!granted.exclusive);
    MEL_EXPECT(!granted.os_timestamps);

    MEL_EXPECT_EQ(mel_audioin_publish_feed(pub.published, block, 32u), 32u);
    MEL_EXPECT_EQ(consumer.frames, 96u);
    MEL_EXPECT_EQ(consumer.samplerate, 48000u);
    MEL_EXPECT_EQ(consumer.channels, 2u);
    MEL_EXPECT_FLOAT_EQ(consumer.first_sample, 0.25f, 0.0f);

    mel_audioin_unpublish(pub.published);
    MEL_EXPECT_EQ(consumer.lost, 1u);
    MEL_EXPECT_EQ(mel_audioin_count(), before);
    MEL_EXPECT_EQ(tally.removed, 1u);
    MEL_EXPECT(!mel_audioin_alive(pub.device));

    mel_audioin_shutdown();
}

MEL_TEST(audioin, publish_feed_rejects_overflow)
{
    install();
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_AudioIn_Publish_Result pub = mel_audioin_publish(a,
                                                         (Mel_AudioIn_Publish_Opt){
                                                             .name = S8("Tiny Feed"),
                                                             .channels = 1,
                                                             .samplerate = 8000,
                                                             .ring_capacity_frames = 16,
                                                         });
    MEL_REQUIRE(!mel_audioin_status_failed(pub.status));

    f32 block[32] = { 0 };
    MEL_EXPECT_EQ(mel_audioin_publish_feed(pub.published, block, 32u), 16u);
    MEL_EXPECT_EQ(mel_audioin_publish_feed(pub.published, block, 4u), 0u);

    mel_audioin_unpublish(pub.published);
    mel_audioin_shutdown();
}

MEL_TEST(audioin, multiple_opens_multiplex_by_token)
{
    install();
    const Mel_Alloc* a = mel_alloc_heap();

    Mel_AudioIn_Publish_Result pub = mel_audioin_publish(a,
                                                         (Mel_AudioIn_Publish_Opt){
                                                             .name = S8("Shared Feed"),
                                                             .channels = 1,
                                                             .samplerate = 48000,
                                                             .ring_capacity_frames = 256,
                                                         });
    MEL_REQUIRE(!mel_audioin_status_failed(pub.status));

    Consume_State    c1 = { .id = 1 };
    Consume_State    c2 = { .id = 2 };
    Mel_AudioIn_Sink    s1 = { .on_frames = consume_on_frames, .on_lost = consume_on_lost, .token = &c1 };
    Mel_AudioIn_Sink    s2 = { .on_frames = consume_on_frames, .on_lost = consume_on_lost, .token = &c2 };
    Mel_AudioIn_Granted g1 = { 0 };
    Mel_AudioIn_Granted g2 = { 0 };
    MEL_EXPECT(!mel_audioin_status_failed(mel_audioin__open(pub.device, s1, (Mel_AudioIn_Open_Opt){ 0 }, &g1)));
    MEL_EXPECT(!mel_audioin_status_failed(mel_audioin__open(pub.device, s2, (Mel_AudioIn_Open_Opt){ 0 }, &g2)));

    f32 block[10] = { 0 };
    mel_audioin_publish_feed(pub.published, block, 10u);
    MEL_EXPECT_EQ(c1.frames, 10u);
    MEL_EXPECT_EQ(c2.frames, 10u);

    mel_audioin__close(pub.device, &c1);
    mel_audioin_publish_feed(pub.published, block, 10u);
    MEL_EXPECT_EQ(c1.frames, 10u);
    MEL_EXPECT_EQ(c2.frames, 20u);

    mel_audioin_unpublish(pub.published);
    MEL_EXPECT_EQ(c1.lost, 0u);
    MEL_EXPECT_EQ(c2.lost, 1u);

    mel_audioin_shutdown();
}
