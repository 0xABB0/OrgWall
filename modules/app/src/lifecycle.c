#include <app/lifecycle.h>
#include <app/subsystem.h>
#include <app/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <event/event.h>
#include <executor/executor.h>
#include <reactor/reactor.h>
#include <thread/thread.h>
#include <time/nano.h>
#include <log/log.h>

#include <assert.h>
#include <string.h>

#define MEL_APP_LIFECYCLE_RING_CAP 32

typedef struct
{
    Mel_App_Provider_Desc desc;
    u32                   generation;
    bool                  active;
    bool                  started;
} Provider_Entry;

typedef struct
{
    u32              refcount;
    const Mel_Alloc* alloc;
    Mel_Reactor*     reactor;
    Mel_Executor*    exec;
    Mel_Thread_Id    loop_thread;
    bool             loop_bound;

    Mel_Event*    events;
    Mel_Event_Sub poll_sub;

    Mel_Array(Provider_Entry) providers;
    u32 provider_gen;

    bool active;
    bool foreground;
} App;

static App g;

static void assert_loop_affinity(void)
{
    if (g.loop_bound)
        assert(mel_thread_id_equal(mel_thread_current_id(), g.loop_thread));
}

static void events_overflow_report(const Mel_Event_Overflow_Info* info, void* user)
{
    (void)user;
    mel_log_warn("app", "lifecycle channel full (capacity %u); dropping oldest, total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

u32 mel_app_init_opt(Mel_App_Init_Opt opt)
{
    if (g.refcount > 0)
    {
        if (opt.alloc && opt.alloc != g.alloc)
            mel_log_warn("app", "init: already initialized; ignoring differing allocator");
        if (opt.reactor && opt.reactor != g.reactor)
            mel_log_warn("app", "init: already initialized; ignoring differing reactor");
        g.refcount++;
        return MEL_APP_OK;
    }

    g.alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    g.reactor = opt.reactor;
    g.exec = opt.reactor ? mel_reactor_executor(opt.reactor) : NULL;
    if (opt.reactor)
    {
        assert(mel_reactor_is_owner(opt.reactor));
        g.loop_thread = mel_thread_current_id();
        g.loop_bound = true;
    }

    g.events = mel_event_create(g.alloc, sizeof(Mel_App_Lifecycle_Event), MEL_APP_LIFECYCLE_RING_CAP, mel_event_policy_lossless(events_overflow_report, NULL));
    if (g.events == NULL)
    {
        mel_log_error("app", "init: lifecycle event channel allocation failed; subsystem unavailable");
        assert(g.events != NULL);
        memset(&g, 0, sizeof g);
        return MEL_APP_ERROR | MEL_APP_UNAVAILABLE;
    }
    g.poll_sub = mel_event_subscribe_pull(g.events, NULL);

    mel_array_init(&g.providers, g.alloc);
    g.provider_gen = 0;
    g.active = true;
    g.foreground = true;
    g.refcount = 1;

    mel_app__register_platform_provider();
    for (usize i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* pe = &g.providers.items[i];
        if (pe->active && !pe->started && pe->desc.start)
        {
            pe->desc.start(pe->desc.user);
            pe->started = true;
        }
    }
    return MEL_APP_OK;
}

u32 mel_app_quit(void)
{
    if (g.refcount == 0)
    {
        mel_log_error("app", "quit without matching init (refcount underflow)");
        assert(g.refcount > 0);
        return MEL_APP_ERROR;
    }
    if (--g.refcount > 0)
        return MEL_APP_OK;

    for (usize i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* pe = &g.providers.items[i];
        if (pe->started && pe->desc.stop)
            pe->desc.stop(pe->desc.user);
        pe->started = false;
    }
    mel_array_free(&g.providers);

    if (g.events != NULL)
    {
        mel_event_unsubscribe(g.events, g.poll_sub);
        mel_event_destroy(g.events);
    }
    memset(&g, 0, sizeof g);
    return MEL_APP_OK;
}

u32  mel_app_refcount(void) { return g.refcount; }
bool mel_app_initialized(void) { return g.refcount > 0; }

Mel_App_Provider mel_app_provider_register(const Mel_App_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true, .started = false };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_App_Provider){ .index = idx, .generation = e.generation };
}

void mel_app_provider_unregister(Mel_App_Provider p)
{
    if (p.index >= g.providers.count || g.providers.items[p.index].generation != p.generation)
        return;
    Provider_Entry* pe = &g.providers.items[p.index];
    if (pe->started && pe->desc.stop)
        pe->desc.stop(pe->desc.user);
    pe->started = false;
    pe->active = false;
}

void mel_app__emit(u32 phase)
{
    if (g.refcount == 0)
        return;
    assert_loop_affinity();

    if (phase & MEL_APP_PHASE_DID_BECOME_ACTIVE)
    {
        g.active = true;
        g.foreground = true;
    }
    if (phase & MEL_APP_PHASE_WILL_RESIGN_ACTIVE)
        g.active = false;
    if (phase & MEL_APP_PHASE_DID_ENTER_BACKGROUND)
    {
        g.active = false;
        g.foreground = false;
    }
    if (phase & MEL_APP_PHASE_WILL_ENTER_FOREGROUND)
        g.foreground = true;

    Mel_App_Lifecycle_Event ev = { .phase = phase, .monotonic_ns = mel_nanos_since_unspecified_epoch() };
    if (g.events != NULL)
        mel_event_fire(g.events, &ev);
}

u32 mel_app_lifecycle_poll(Mel_App_Lifecycle_Event* out, u32 cap)
{
    if (g.refcount == 0 || g.events == NULL)
        return 0;
    u32 n = 0;
    for (; n < cap && mel_event_pull(g.events, g.poll_sub, &out[n]); n++)
        ;
    return n;
}

Mel_App_Lifecycle_Subscription mel_app_lifecycle_subscribe(Mel_Executor* deliver, Mel_App_Lifecycle_Callback cb, void* user)
{
    if (g.refcount == 0 || g.events == NULL)
    {
        mel_log_error("app", "lifecycle subscribe before init; no channel");
        return MEL_APP_LIFECYCLE_SUBSCRIPTION_NULL;
    }
    Mel_Executor* target = deliver != NULL ? deliver : g.exec;
    if (target == NULL)
    {
        mel_log_error("app", "lifecycle subscribe needs an executor; none passed and subsystem has none (no reactor at init)");
        return MEL_APP_LIFECYCLE_SUBSCRIPTION_NULL;
    }
    Mel_Event_Sub sub = mel_event_subscribe_push(g.events, target, (Mel_Event_Callback)cb, user);
    return (Mel_App_Lifecycle_Subscription){ sub.handle };
}

void mel_app_lifecycle_unsubscribe(Mel_App_Lifecycle_Subscription sub)
{
    if (g.refcount == 0 || g.events == NULL)
        return;
    mel_event_unsubscribe(g.events, (Mel_Event_Sub){ sub.handle });
}

bool mel_app_active(void) { return g.refcount > 0 && g.active; }
bool mel_app_foreground(void) { return g.refcount > 0 && g.foreground; }

Mel_Reactor* mel_app__reactor(void) { return g.reactor; }
