#include "boot_internal.h"

#include <boot/lifecycle.h>

#include <allocator/allocator.h>
#include <debug/assert.h>
#include <event/event.h>
#include <executor/executor.h>
#include <log/log.h>
#include <time/nano.h>
#include <vat/vat.h>

#define BOOT_LIFECYCLE_RING_CAP 32

typedef struct
{
    Mel_Vat*      vat;
    Mel_Event*    events;
    Mel_Event_Sub poll_sub;
    bool          active;
    bool          foreground;
    bool          live;
} Boot_Lifecycle;

static Boot_Lifecycle g_life;

static void events_overflow_report(const Mel_Event_Overflow_Info* info, void* user)
{
    (void)user;
    mel_log_warn("boot", "lifecycle channel full (capacity %u); total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

void mel_boot__lifecycle_init(Mel_Vat* vat, const Mel_Alloc* alloc)
{
    mel_assert(!g_life.live);
    g_life.vat = vat;
    g_life.events = mel_event_create(alloc, sizeof(Mel_App_Lifecycle_Event), BOOT_LIFECYCLE_RING_CAP, mel_event_policy_lossless(events_overflow_report, NULL));
    mel_assert(g_life.events != NULL);
    g_life.poll_sub = mel_event_subscribe_pull(g_life.events, NULL);
    g_life.active = true;
    g_life.foreground = true;
    g_life.live = true;
    mel_boot__lifecycle_platform_start();
}

void mel_boot__lifecycle_shutdown(void)
{
    if (!g_life.live)
        return;
    mel_boot__lifecycle_platform_stop();
    mel_event_unsubscribe(g_life.events, g_life.poll_sub);
    mel_event_destroy(g_life.events);
    g_life = (Boot_Lifecycle){ 0 };
}

void mel_app__emit(u32 phase)
{
    if (!g_life.live)
        return;
    mel_assert(mel_vat_is_owner(g_life.vat));

    if (phase & MEL_APP_PHASE_DID_BECOME_ACTIVE)
    {
        g_life.active = true;
        g_life.foreground = true;
    }
    if (phase & MEL_APP_PHASE_WILL_RESIGN_ACTIVE)
        g_life.active = false;
    if (phase & MEL_APP_PHASE_DID_ENTER_BACKGROUND)
    {
        g_life.active = false;
        g_life.foreground = false;
    }
    if (phase & MEL_APP_PHASE_WILL_ENTER_FOREGROUND)
        g_life.foreground = true;

    Mel_App_Lifecycle_Event ev = { .phase = phase, .monotonic_ns = mel_nanos_since_unspecified_epoch() };
    mel_event_fire(g_life.events, &ev);
}

u32 mel_app_lifecycle_poll(Mel_App_Lifecycle_Event* out, u32 cap)
{
    if (!g_life.live)
        return 0;
    u32 n = 0;
    for (; n < cap && mel_event_pull(g_life.events, g_life.poll_sub, &out[n]); n++)
        ;
    return n;
}

Mel_App_Lifecycle_Subscription mel_app_lifecycle_subscribe(Mel_Executor* deliver, Mel_App_Lifecycle_Callback cb, void* user)
{
    if (!g_life.live)
    {
        mel_log_error("boot", "lifecycle subscribe before entry; no channel");
        return MEL_APP_LIFECYCLE_SUBSCRIPTION_NULL;
    }
    Mel_Executor* target = deliver != NULL ? deliver : mel_vat_executor(g_life.vat);
    Mel_Event_Sub sub = mel_event_subscribe_push(g_life.events, target, (Mel_Event_Callback)cb, user);
    return (Mel_App_Lifecycle_Subscription){ sub.handle };
}

void mel_app_lifecycle_unsubscribe(Mel_App_Lifecycle_Subscription sub)
{
    if (!g_life.live)
        return;
    mel_event_unsubscribe(g_life.events, (Mel_Event_Sub){ sub.handle });
}

bool mel_app_active(void) { return g_life.live && g_life.active; }

bool mel_app_foreground(void) { return g_life.live && g_life.foreground; }
