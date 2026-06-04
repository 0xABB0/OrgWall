#include <app/lifecycle.h>
#include <app/subsystem.h>
#include <app/provider.h>
#include <test/test.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>
#include <reactor/reactor.h>

#include <string.h>

static int fake_started;
static int fake_stopped;

static void fake_start(void* user)
{
    (void)user;
    fake_started++;
}

static void fake_stop(void* user)
{
    (void)user;
    fake_stopped++;
}

void mel_app__register_platform_provider(void)
{
    Mel_App_Provider_Desc desc = { .name = "test-fake", .start = fake_start, .stop = fake_stop };
    mel_app_provider_register(&desc);
}

static void reset_fake(void)
{
    fake_started = 0;
    fake_stopped = 0;
}

MEL_TEST(app, init_quit_refcount_balances)
{
    reset_fake();
    MEL_EXPECT(!mel_app_initialized());
    MEL_EXPECT_EQ((i64)mel_app_refcount(), (i64)0);

    mel_app_init(.alloc = mel_alloc_heap());
    MEL_EXPECT(mel_app_initialized());
    MEL_EXPECT_EQ((i64)mel_app_refcount(), (i64)1);
    MEL_EXPECT_EQ((i64)fake_started, (i64)1);

    mel_app_init(.alloc = mel_alloc_heap());
    MEL_EXPECT_EQ((i64)mel_app_refcount(), (i64)2);
    MEL_EXPECT_EQ((i64)fake_started, (i64)1);

    mel_app_quit();
    MEL_EXPECT(mel_app_initialized());
    MEL_EXPECT_EQ((i64)mel_app_refcount(), (i64)1);
    MEL_EXPECT_EQ((i64)fake_stopped, (i64)0);

    mel_app_quit();
    MEL_EXPECT(!mel_app_initialized());
    MEL_EXPECT_EQ((i64)mel_app_refcount(), (i64)0);
    MEL_EXPECT_EQ((i64)fake_stopped, (i64)1);
}

MEL_TEST(app, poll_drains_emitted_phases_in_order)
{
    reset_fake();
    mel_app_init(.alloc = mel_alloc_heap());

    mel_app__emit(MEL_APP_PHASE_WILL_RESIGN_ACTIVE);
    mel_app__emit(MEL_APP_PHASE_DID_ENTER_BACKGROUND);
    mel_app__emit(MEL_APP_PHASE_WILL_TERMINATE);

    Mel_App_Lifecycle_Event evs[8];
    u32                     n = mel_app_lifecycle_poll(evs, 8);
    MEL_REQUIRE_EQ((i64)n, (i64)3);
    MEL_EXPECT(evs[0].phase == MEL_APP_PHASE_WILL_RESIGN_ACTIVE);
    MEL_EXPECT(evs[1].phase == MEL_APP_PHASE_DID_ENTER_BACKGROUND);
    MEL_EXPECT(evs[2].phase == MEL_APP_PHASE_WILL_TERMINATE);

    u32 again = mel_app_lifecycle_poll(evs, 8);
    MEL_EXPECT_EQ((i64)again, (i64)0);

    mel_app_quit();
}

MEL_TEST(app, active_foreground_state_tracks_phases)
{
    reset_fake();
    mel_app_init(.alloc = mel_alloc_heap());
    MEL_EXPECT(mel_app_active());
    MEL_EXPECT(mel_app_foreground());

    mel_app__emit(MEL_APP_PHASE_WILL_RESIGN_ACTIVE);
    MEL_EXPECT(!mel_app_active());
    MEL_EXPECT(mel_app_foreground());

    mel_app__emit(MEL_APP_PHASE_DID_ENTER_BACKGROUND);
    MEL_EXPECT(!mel_app_active());
    MEL_EXPECT(!mel_app_foreground());

    mel_app__emit(MEL_APP_PHASE_WILL_ENTER_FOREGROUND);
    MEL_EXPECT(!mel_app_active());
    MEL_EXPECT(mel_app_foreground());

    mel_app__emit(MEL_APP_PHASE_DID_BECOME_ACTIVE);
    MEL_EXPECT(mel_app_active());
    MEL_EXPECT(mel_app_foreground());

    mel_app_quit();
}

MEL_TEST(app, subscribe_before_init_returns_null)
{
    reset_fake();
    Mel_App_Lifecycle_Subscription sub = mel_app_lifecycle_subscribe(mel_executor_inline(), NULL, NULL);
    MEL_EXPECT(sub.handle.index == MEL_APP_LIFECYCLE_SUBSCRIPTION_NULL.handle.index);
    MEL_EXPECT(sub.handle.generation == MEL_APP_LIFECYCLE_SUBSCRIPTION_NULL.handle.generation);
}

MEL_TEST(app, subscribe_without_executor_or_reactor_rejected)
{
    reset_fake();
    mel_app_init(.alloc = mel_alloc_heap());
    Mel_App_Lifecycle_Subscription sub = mel_app_lifecycle_subscribe(NULL, NULL, NULL);
    MEL_EXPECT(sub.handle.index == MEL_APP_LIFECYCLE_SUBSCRIPTION_NULL.handle.index);
    MEL_EXPECT(sub.handle.generation == MEL_APP_LIFECYCLE_SUBSCRIPTION_NULL.handle.generation);
    mel_app_quit();
}

typedef struct
{
    u32 seen_phase;
    u32 count;
} Capture;

static void capture_cb(const Mel_App_Lifecycle_Event* ev, void* user)
{
    Capture* c = (Capture*)user;
    c->seen_phase |= ev->phase;
    c->count++;
}

typedef struct
{
    Mel_Reactor*                   reactor;
    int                            turn;
    Capture                        cap;
    Mel_App_Lifecycle_Subscription sub;
    bool                           done;
} Async_Ctx;

static bool async_idle(void* user)
{
    Async_Ctx* a = (Async_Ctx*)user;
    a->turn++;
    if (a->turn == 1)
    {
        a->sub = mel_app_lifecycle_subscribe(mel_reactor_executor(a->reactor), capture_cb, &a->cap);
        mel_app__emit(MEL_APP_PHASE_LOW_MEMORY);
    }
    if (a->turn == 4 && a->cap.count > 0)
    {
        mel_app_lifecycle_unsubscribe(a->sub);
        a->done = true;
    }
    if (a->done || a->turn > 5000)
        mel_reactor_quit(a->reactor);
    return true;
}

static bool async_setup(Mel_Reactor* r, void* user)
{
    Async_Ctx* a = (Async_Ctx*)user;
    a->reactor = r;
    mel_app_init(.alloc = mel_alloc_heap(), .reactor = r);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(async_idle, a);
    mel_reactor_source_attach(r, idle);
    return true;
}

MEL_TEST(app, push_subscription_delivers_on_executor)
{
    reset_fake();
    Async_Ctx a = { 0 };
    mel_reactor_spawn(MEL_REACTOR_THREADED, async_setup, &a);

    MEL_EXPECT(a.done);
    MEL_EXPECT_EQ((i64)a.cap.count, (i64)1);
    MEL_EXPECT((a.cap.seen_phase & MEL_APP_PHASE_LOW_MEMORY) != 0u);
    mel_app_quit();
}

MEL_TEST(app, status_predicates_classify_severity)
{
    MEL_EXPECT(mel_app_status_ok(MEL_APP_OK));
    MEL_EXPECT(mel_app_status_warned(MEL_APP_WARNED));
    MEL_EXPECT(mel_app_status_failed(MEL_APP_ERROR));
    MEL_EXPECT(mel_app_status_failed(MEL_APP_ERROR | MEL_APP_NO_BACKEND));
    MEL_EXPECT(!mel_app_status_ok(MEL_APP_ERROR));
}
