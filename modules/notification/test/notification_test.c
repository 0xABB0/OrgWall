#include <test/test.h>

#include <notification/notification.h>
#include <notification/events.h>
#include <notification/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>
#include <future/future.h>
#include <string/str8.h>
#include <collection/slotmap.h>

#include <string.h>

#include "../src/notification_internal.h"

typedef struct
{
    u32                   posts;
    u32                   updates;
    u32                   cancels;
    u32                   cancel_alls;
    u32                   channels;
    u64                   last_token;
    bool                  last_scheduled;
    u64                   last_at;
    u64                   last_interval;
    u32                   last_action_count;
    char                  last_title[64];
    char                  last_channel[64];
    Mel_Notif_Caps        caps;
    const mel_notif_auth* auth;
    Mel_Notif_Sink        pending_sink;
    bool                  sink_pending;
} Fake_State;

static Fake_State fk;

static bool fake_supported(void* u)
{
    (void)u;
    return true;
}
static Mel_Notif_Caps fake_caps(void* u)
{
    (void)u;
    return fk.caps;
}
static const mel_notif_auth* fake_authorization(void* u)
{
    (void)u;
    return fk.auth;
}
static void fake_authorize(void* u, Mel_Notif_Sink sink)
{
    (void)u;
    fk.pending_sink = sink;
    fk.sink_pending = true;
}
static Mel_Notif_Status fake_channel_register(void* u, const Mel_Notif_Channel_Opt* opt)
{
    (void)u;
    fk.channels++;
    str8_to_buf(opt->id, fk.last_channel, sizeof fk.last_channel);
    return MEL_NOTIF_OK;
}
static Mel_Notif_Status fake_post(void* u, const Mel_Notif_Lowered* lw)
{
    (void)u;
    fk.posts++;
    fk.last_token = lw->token;
    fk.last_scheduled = lw->scheduled;
    fk.last_at = lw->trigger.at_unix_ms;
    fk.last_interval = lw->trigger.interval_ms;
    fk.last_action_count = lw->content->action_count;
    str8_to_buf(lw->content->title, fk.last_title, sizeof fk.last_title);
    str8_to_buf(lw->content->channel, fk.last_channel, sizeof fk.last_channel);
    return MEL_NOTIF_OK;
}
static Mel_Notif_Status fake_update(void* u, const Mel_Notif_Lowered* lw)
{
    (void)u;
    fk.updates++;
    str8_to_buf(lw->content->title, fk.last_title, sizeof fk.last_title);
    return MEL_NOTIF_OK;
}
static void fake_cancel(void* u, u64 token)
{
    (void)u;
    fk.cancels++;
    fk.last_token = token;
}
static void fake_cancel_all(void* u)
{
    (void)u;
    fk.cancel_alls++;
}

#define FAKE_CAPS_ALL (MEL_NOTIF_CAP_ACTIONS | MEL_NOTIF_CAP_REPLY | MEL_NOTIF_CAP_ICON | MEL_NOTIF_CAP_ATTACHMENT | MEL_NOTIF_CAP_PROGRESS | MEL_NOTIF_CAP_BADGE | MEL_NOTIF_CAP_SOUND | MEL_NOTIF_CAP_SCHEDULE | MEL_NOTIF_CAP_SCHEDULE_PERSISTS | MEL_NOTIF_CAP_REPEAT | MEL_NOTIF_CAP_UPDATE)

static Mel_Notif_Provider install_fake(bool with_update)
{
    Mel_Notif_Provider_Desc desc = {
        .name = "fake",
        .supported = fake_supported,
        .caps = fake_caps,
        .authorization = fake_authorization,
        .authorize = fake_authorize,
        .channel_register = fake_channel_register,
        .post = fake_post,
        .update = with_update ? fake_update : NULL,
        .cancel = fake_cancel,
        .cancel_all = fake_cancel_all,
    };
    Mel_Notif_Provider p = mel_notif_provider_register(&desc);
    mel_notif__force_provider(p);
    return p;
}

static void begin(void)
{
    memset(&fk, 0, sizeof fk);
    fk.caps = FAKE_CAPS_ALL;
    fk.auth = &mel_notif_auth_granted;
    mel_notif__init_bare(mel_alloc_heap(), mel_executor_inline());
    install_fake(true);
}

MEL_TEST(notification, null_handle_is_dead)
{
    Mel_Notif null = MEL_NOTIF_NULL;
    MEL_EXPECT(!mel_notif_alive(null));
    MEL_EXPECT(mel_notif_equal(null, null));
}

MEL_TEST(notification, no_provider_post_fails_loud)
{
    memset(&fk, 0, sizeof fk);
    mel_notif__init_bare(mel_alloc_heap(), NULL);
    Mel_Notif_Content c = { .title = S8("x") };
    Mel_Notif_Result  r = mel_notif_post(&c);
    MEL_EXPECT(mel_notif_failed(r.status));
    MEL_EXPECT((r.status & MEL_NOTIF_ERR_NO_PROVIDER) != 0);
    MEL_EXPECT(!mel_notif_supported());
    mel_notif_shutdown();
}

MEL_TEST(notification, post_lowers_content_and_mints_handle)
{
    begin();
    Mel_Notif_Action  acts[] = { { .id = S8("ok"), .label = S8("OK") }, { .id = S8("no"), .label = S8("No"), .flags = MEL_NOTIF_ACTION_DESTRUCTIVE } };
    Mel_Notif_Content c = { .title = S8("hello"), .body = S8("world"), .actions = acts, .action_count = 2 };
    Mel_Notif_Result  r = mel_notif_post(&c);
    MEL_REQUIRE(!mel_notif_failed(r.status));
    MEL_REQUIRE(mel_notif_alive(r.value));
    MEL_EXPECT_EQ(fk.posts, 1u);
    MEL_EXPECT_EQ(fk.last_scheduled, false);
    MEL_EXPECT_EQ(fk.last_action_count, 2u);
    MEL_EXPECT(strcmp(fk.last_title, "hello") == 0);
    mel_notif_shutdown();
}

MEL_TEST(notification, post_copies_content_caller_storage_free)
{
    begin();
    char title[16];
    strcpy(title, "ephemeral");
    Mel_Notif_Content c = { .title = { (u8*)title, 9 } };
    Mel_Notif_Result  r = mel_notif_post(&c);
    MEL_REQUIRE(!mel_notif_failed(r.status));
    memset(title, 0, sizeof title);
    Notif_Slot* ns = mel_notif__slot(r.value.h);
    MEL_REQUIRE(ns != NULL);
    MEL_EXPECT(str8_equals(ns->content.title, S8("ephemeral")));
    mel_notif_shutdown();
}

MEL_TEST(notification, caps_warns_on_dropped_features)
{
    begin();
    fk.caps = MEL_NOTIF_CAP_SCHEDULE;
    Mel_Notif_Action  acts[] = { { .id = S8("a"), .label = S8("A"), .flags = MEL_NOTIF_ACTION_TEXT_INPUT } };
    Mel_Notif_Content c = {
        .title = S8("t"),
        .actions = acts,
        .action_count = 1,
        .progress = { .present = true, .value = 0.5f },
        .has_badge = true,
        .badge = 3,
        .sound_path = S8("ding.wav"),
    };
    Mel_Notif_Result r = mel_notif_post(&c);
    MEL_REQUIRE(!mel_notif_failed(r.status));
    MEL_EXPECT(mel_notif_warned(r.status));
    MEL_EXPECT((r.status & MEL_NOTIF_WARN_ACTIONS_DROPPED) != 0);
    MEL_EXPECT((r.status & MEL_NOTIF_WARN_REPLY_DROPPED) != 0);
    MEL_EXPECT((r.status & MEL_NOTIF_WARN_PROGRESS_DROPPED) != 0);
    MEL_EXPECT((r.status & MEL_NOTIF_WARN_BADGE_DROPPED) != 0);
    MEL_EXPECT((r.status & MEL_NOTIF_WARN_SOUND_DROPPED) != 0);
    mel_notif_shutdown();
}

MEL_TEST(notification, schedule_passes_trigger_and_flags_volatile)
{
    begin();
    Mel_Notif_Content c = { .title = S8("later") };
    Mel_Notif_Result  r = mel_notif_schedule(&c, (Mel_Notif_Trigger){ .at_unix_ms = 1111u, .interval_ms = 2222u });
    MEL_REQUIRE(!mel_notif_failed(r.status));
    MEL_EXPECT_EQ(fk.last_scheduled, true);
    MEL_EXPECT_EQ(fk.last_at, 1111u);
    MEL_EXPECT_EQ(fk.last_interval, 2222u);
    MEL_EXPECT(!mel_notif_warned(r.status));

    fk.caps = MEL_NOTIF_CAP_SCHEDULE;
    Mel_Notif_Result v = mel_notif_schedule(&c, (Mel_Notif_Trigger){ .at_unix_ms = 1111u, .interval_ms = 2222u });
    MEL_REQUIRE(!mel_notif_failed(v.status));
    MEL_EXPECT((v.status & MEL_NOTIF_WARN_SCHEDULE_VOLATILE) != 0);
    MEL_EXPECT((v.status & MEL_NOTIF_WARN_REPEAT_CLAMPED) != 0);

    fk.caps = 0;
    Mel_Notif_Result u = mel_notif_schedule(&c, (Mel_Notif_Trigger){ .at_unix_ms = 1111u });
    MEL_EXPECT(mel_notif_failed(u.status));
    MEL_EXPECT((u.status & MEL_NOTIF_ERR_UNSUPPORTED) != 0);

    Mel_Notif_Result e = mel_notif_schedule(&c, (Mel_Notif_Trigger){ 0 });
    MEL_EXPECT(mel_notif_failed(e.status));
    MEL_EXPECT((e.status & MEL_NOTIF_ERR_INVALID_ARG) != 0);
    mel_notif_shutdown();
}

MEL_TEST(notification, update_reaches_provider_or_reposts)
{
    begin();
    Mel_Notif_Content c = { .title = S8("v1") };
    Mel_Notif_Result  r = mel_notif_post(&c);
    MEL_REQUIRE(!mel_notif_failed(r.status));

    Mel_Notif_Content c2 = { .title = S8("v2") };
    MEL_EXPECT_EQ(mel_notif_update(r.value, &c2), MEL_NOTIF_OK);
    MEL_EXPECT_EQ(fk.updates, 1u);
    MEL_EXPECT(strcmp(fk.last_title, "v2") == 0);
    mel_notif_shutdown();

    memset(&fk, 0, sizeof fk);
    fk.caps = FAKE_CAPS_ALL;
    fk.auth = &mel_notif_auth_granted;
    mel_notif__init_bare(mel_alloc_heap(), mel_executor_inline());
    install_fake(false);
    Mel_Notif_Result r2 = mel_notif_post(&c);
    MEL_REQUIRE(!mel_notif_failed(r2.status));
    Mel_Notif_Status s = mel_notif_update(r2.value, &c2);
    MEL_EXPECT(mel_notif_warned(s));
    MEL_EXPECT((s & MEL_NOTIF_WARN_UPDATE_REPOSTED) != 0);
    MEL_EXPECT_EQ(fk.posts, 2u);
    mel_notif_shutdown();
}

MEL_TEST(notification, update_dead_handle_is_loud_error)
{
    begin();
    Mel_Notif         bogus = { .h = { .index = 9999, .generation = 7 } };
    Mel_Notif_Content c = { .title = S8("x") };
    Mel_Notif_Status  s = mel_notif_update(bogus, &c);
    MEL_EXPECT(mel_notif_failed(s));
    MEL_EXPECT((s & MEL_NOTIF_ERR_DEAD_HANDLE) != 0);
    mel_notif_shutdown();
}

MEL_TEST(notification, cancel_reaches_provider_and_kills_handle)
{
    begin();
    Mel_Notif_Content c = { .title = S8("x") };
    Mel_Notif_Result  r = mel_notif_post(&c);
    MEL_REQUIRE(mel_notif_alive(r.value));
    u64 token = mel_slotmap_handle_pack64(r.value.h);
    MEL_EXPECT_EQ(mel_notif_cancel(r.value), MEL_NOTIF_OK);
    MEL_EXPECT(!mel_notif_alive(r.value));
    MEL_EXPECT_EQ(fk.cancels, 1u);
    MEL_EXPECT_EQ(fk.last_token, token);
    MEL_EXPECT(mel_notif_failed(mel_notif_cancel(r.value)));
    mel_notif_shutdown();
}

MEL_TEST(notification, cancel_all_sweeps)
{
    begin();
    Mel_Notif_Content c = { .title = S8("x") };
    Mel_Notif_Result  a = mel_notif_post(&c);
    Mel_Notif_Result  b = mel_notif_post(&c);
    mel_notif_cancel_all();
    MEL_EXPECT_EQ(fk.cancel_alls, 1u);
    MEL_EXPECT(!mel_notif_alive(a.value));
    MEL_EXPECT(!mel_notif_alive(b.value));
    mel_notif_shutdown();
}

MEL_TEST(notification, channels_required_when_capable)
{
    begin();
    fk.caps = FAKE_CAPS_ALL | MEL_NOTIF_CAP_CHANNELS;

    Mel_Notif_Content unreg = { .title = S8("x"), .channel = S8("nope") };
    Mel_Notif_Result  bad = mel_notif_post(&unreg);
    MEL_EXPECT(mel_notif_failed(bad.status));
    MEL_EXPECT((bad.status & MEL_NOTIF_ERR_INVALID_ARG) != 0);

    MEL_EXPECT_EQ(mel_notif_channel_register((Mel_Notif_Channel_Opt){ .id = S8("news"), .label = S8("News") }), MEL_NOTIF_OK);
    MEL_EXPECT_EQ(fk.channels, 1u);
    Mel_Notif_Content reg = { .title = S8("x"), .channel = S8("news") };
    Mel_Notif_Result  ok = mel_notif_post(&reg);
    MEL_REQUIRE(!mel_notif_failed(ok.status));
    MEL_EXPECT(strcmp(fk.last_channel, "news") == 0);

    Mel_Notif_Content nochan = { .title = S8("x") };
    Mel_Notif_Result  def = mel_notif_post(&nochan);
    MEL_REQUIRE(!mel_notif_failed(def.status));
    MEL_EXPECT((def.status & MEL_NOTIF_WARN_DEFAULT_CHANNEL) != 0);
    MEL_EXPECT(strcmp(fk.last_channel, "melody.default") == 0);

    MEL_EXPECT(mel_notif_failed(mel_notif_channel_register((Mel_Notif_Channel_Opt){ 0 })));
    mel_notif_shutdown();
}

MEL_TEST(notification, channel_ignored_without_cap)
{
    begin();
    Mel_Notif_Content c = { .title = S8("x"), .channel = S8("whatever") };
    Mel_Notif_Result  r = mel_notif_post(&c);
    MEL_REQUIRE(!mel_notif_failed(r.status));
    MEL_EXPECT(!mel_notif_warned(r.status));
    mel_notif_shutdown();
}

MEL_TEST(notification, authorize_resolves_via_sink)
{
    begin();
    fk.auth = &mel_notif_auth_not_determined;
    MEL_EXPECT(mel_notif_authorization() == &mel_notif_auth_not_determined);

    Mel_Future* f = mel_notif_authorize(NULL);
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(!mel_future_resolved(f));
    MEL_REQUIRE(fk.sink_pending);

    fk.pending_sink.on_auth(fk.pending_sink.token, &mel_notif_auth_granted);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT(mel_notif_future_auth(f) == &mel_notif_auth_granted);
    MEL_EXPECT(mel_notif_auth_is_granted(mel_notif_future_auth(f)));
    mel_notif_shutdown();
}

MEL_TEST(notification, authorize_without_provider_resolves_not_determined)
{
    memset(&fk, 0, sizeof fk);
    mel_notif__init_bare(mel_alloc_heap(), NULL);
    Mel_Future* f = mel_notif_authorize(NULL);
    MEL_REQUIRE(f != NULL);
    MEL_EXPECT(mel_future_resolved(f));
    MEL_EXPECT(mel_notif_future_auth(f) == &mel_notif_auth_not_determined);
    mel_notif_shutdown();
}

MEL_TEST(notification, activation_events_carry_action_reply_payload)
{
    begin();
    Mel_Notif_Content c = { .title = S8("t"), .payload = S8("user-data") };
    Mel_Notif_Result  r = mel_notif_post(&c);
    MEL_REQUIRE(!mel_notif_failed(r.status));

    Mel_Notif_Event drain[16];
    mel_notif_poll_events(drain, 16);

    char reply_buf[8];
    strcpy(reply_buf, "yo");
    mel_notif__dispatch_activated(mel_slotmap_handle_pack64(r.value.h), S8("reply-action"), (str8){ (u8*)reply_buf, 2 }, STR8_EMPTY);
    memset(reply_buf, 0, sizeof reply_buf);

    u32 got = mel_notif_poll_events(drain, 16);
    MEL_REQUIRE_EQ(got, 1u);
    MEL_EXPECT((drain[0].kind & MEL_NOTIF_EVENT_ACTIVATED) != 0);
    MEL_EXPECT((drain[0].kind & MEL_NOTIF_EVENT_ACTION) != 0);
    MEL_EXPECT((drain[0].kind & MEL_NOTIF_EVENT_REPLIED) != 0);
    MEL_EXPECT(mel_notif_equal(drain[0].notif, r.value));
    MEL_EXPECT(str8_equals(drain[0].action_id, S8("reply-action")));
    MEL_EXPECT(str8_equals(drain[0].reply, S8("yo")));
    MEL_EXPECT(str8_equals(drain[0].payload, S8("user-data")));
    mel_notif_shutdown();
}

MEL_TEST(notification, plain_tap_is_activated_only)
{
    begin();
    Mel_Notif_Content c = { .title = S8("t") };
    Mel_Notif_Result  r = mel_notif_post(&c);

    Mel_Notif_Event drain[16];
    mel_notif_poll_events(drain, 16);

    mel_notif__dispatch_activated(mel_slotmap_handle_pack64(r.value.h), STR8_EMPTY, STR8_EMPTY, STR8_EMPTY);
    u32 got = mel_notif_poll_events(drain, 16);
    MEL_REQUIRE_EQ(got, 1u);
    MEL_EXPECT_EQ(drain[0].kind, (Mel_Notif_Event_Kind)MEL_NOTIF_EVENT_ACTIVATED);
    MEL_EXPECT(drain[0].reply.len == 0);
    mel_notif_shutdown();
}

MEL_TEST(notification, unknown_token_activation_still_delivers_payload)
{
    begin();
    Mel_Notif_Event drain[16];
    mel_notif_poll_events(drain, 16);

    mel_notif__dispatch_activated(0xdeadbeefull, S8("a"), STR8_EMPTY, S8("from-os"));
    u32 got = mel_notif_poll_events(drain, 16);
    MEL_REQUIRE_EQ(got, 1u);
    MEL_EXPECT(!mel_notif_alive(drain[0].notif));
    MEL_EXPECT(str8_equals(drain[0].payload, S8("from-os")));
    mel_notif_shutdown();
}

MEL_TEST(notification, dismiss_and_presented_events)
{
    begin();
    Mel_Notif_Content c = { .title = S8("t") };
    Mel_Notif_Result  r = mel_notif_post(&c);

    Mel_Notif_Event drain[16];
    mel_notif_poll_events(drain, 16);

    mel_notif__dispatch_presented(mel_slotmap_handle_pack64(r.value.h));
    mel_notif__dispatch_dismissed(mel_slotmap_handle_pack64(r.value.h));
    u32 got = mel_notif_poll_events(drain, 16);
    MEL_REQUIRE_EQ(got, 2u);
    MEL_EXPECT_EQ(drain[0].kind, (Mel_Notif_Event_Kind)MEL_NOTIF_EVENT_PRESENTED);
    MEL_EXPECT_EQ(drain[1].kind, (Mel_Notif_Event_Kind)MEL_NOTIF_EVENT_DISMISSED);
    mel_notif_shutdown();
}

typedef struct
{
    u32                   auth_changes;
    u32                   tokens;
    u32                   pushes;
    const mel_notif_auth* last_auth;
} Push_Sink;

static void push_cb(const Mel_Notif_Event* ev, void* user)
{
    Push_Sink* s = (Push_Sink*)user;
    if ((ev->kind & MEL_NOTIF_EVENT_AUTH_CHANGED) != 0)
    {
        s->auth_changes++;
        s->last_auth = ev->auth;
    }
    if ((ev->kind & MEL_NOTIF_EVENT_PUSH_TOKEN) != 0)
        s->tokens++;
    if ((ev->kind & MEL_NOTIF_EVENT_PUSH_RECEIVED) != 0)
        s->pushes++;
}

MEL_TEST(notification, subscribe_push_delivers_auth_and_push_events)
{
    begin();
    Push_Sink              sink = { 0 };
    Mel_Notif_Subscription sub = mel_notif_subscribe(mel_executor_inline(), push_cb, &sink);
    MEL_REQUIRE(mel_slotmap_handle_valid(sub.handle));

    mel_notif__dispatch_auth_changed(&mel_notif_auth_denied);
    mel_notif__dispatch_auth_changed(&mel_notif_auth_denied);
    MEL_EXPECT_EQ(sink.auth_changes, 1u);
    MEL_EXPECT(sink.last_auth == &mel_notif_auth_denied);

    mel_notif__dispatch_push_token(S8("tok-bytes"));
    mel_notif__dispatch_push(S8("{\"k\":1}"));
    MEL_EXPECT_EQ(sink.tokens, 1u);
    MEL_EXPECT_EQ(sink.pushes, 1u);

    mel_notif_unsubscribe(sub);
    mel_notif_shutdown();
}

MEL_TEST(notification, subscribe_without_executor_is_loud_null)
{
    memset(&fk, 0, sizeof fk);
    mel_notif__init_bare(mel_alloc_heap(), NULL);
    Push_Sink              sink = { 0 };
    Mel_Notif_Subscription sub = mel_notif_subscribe(NULL, push_cb, &sink);
    MEL_EXPECT(!mel_slotmap_handle_valid(sub.handle));
    mel_notif_shutdown();
}

MEL_TEST(notification, event_blob_storm_does_not_leak_or_crash)
{
    begin();
    for (u32 i = 0; i < 1000; i++)
        mel_notif__dispatch_push(S8("payload-payload-payload"));
    Mel_Notif_Event drain[32];
    u32             got = mel_notif_poll_events(drain, 32);
    MEL_EXPECT(got > 0);
    for (u32 i = 0; i < got; i++)
        MEL_EXPECT(str8_equals(drain[i].payload, S8("payload-payload-payload")));
    mel_notif_shutdown();
}
