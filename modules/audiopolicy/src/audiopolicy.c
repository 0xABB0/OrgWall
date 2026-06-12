#include "audiopolicy_internal.h"

#include <allocator/allocator.h>
#include <executor/executor.h>
#include <event/event.h>
#include <log/log.h>

#include <string.h>

#define MEL_AUDIOPOLICY_EVENT_RING_CAP 32u

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Executor*    exec;
    Mel_Event*       events;
    Mel_AudioPolicy  in_force;
    bool             applied;
    bool             focus_held;
} Policy;

static Policy g;

static void event_overflow(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    mel_log_warn("audiopolicy", "event channel full (capacity %u); total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

void mel_audiopolicy_init(const Mel_Alloc* alloc, Mel_Executor* deliver)
{
    assert(!g.initialized);
    assert(alloc != NULL);
    if (g.initialized)
        return;
    g.alloc = alloc;
    g.exec = deliver ? deliver : mel_executor_inline();
    g.events = mel_event_create(g.alloc, sizeof(Mel_AudioPolicy_Event), MEL_AUDIOPOLICY_EVENT_RING_CAP, mel_event_policy_latest(event_overflow, NULL));
    g.in_force = (Mel_AudioPolicy){ 0 };
    g.applied = false;
    g.focus_held = false;
    g.initialized = true;
    const Mel_AudioPolicy_Backend* b = mel_audiopolicy__backend();
    if (b->startup)
        b->startup();
}

void mel_audiopolicy_shutdown(void)
{
    assert(g.initialized);
    if (!g.initialized)
        return;
    const Mel_AudioPolicy_Backend* b = mel_audiopolicy__backend();
    if (g.focus_held && b->focus_abandon)
        b->focus_abandon();
    if (b->shutdown)
        b->shutdown();
    mel_event_destroy(g.events);
    memset(&g, 0, sizeof g);
}

Mel_AudioPolicy_Status mel_audiopolicy_apply(Mel_AudioPolicy policy)
{
    assert(g.initialized);
    assert(policy.category != NULL);
    if (!g.initialized)
        return MEL_AUDIOPOLICY_ERROR | MEL_AUDIOPOLICY_RESULT_UNSUPPORTED;
    if (policy.category == NULL)
    {
        mel_log_error("audiopolicy", "apply with NULL category: applying policy means saying what you are");
        return MEL_AUDIOPOLICY_ERROR | MEL_AUDIOPOLICY_RESULT_UNSUPPORTED;
    }
    if (policy.mode == NULL)
        policy.mode = &mel_audiopolicy_mode_default;

    Mel_AudioPolicy in_force = policy;

    Mel_AudioPolicy_Status st = mel_audiopolicy__backend()->apply(&policy, &in_force);
    if (mel_audiopolicy_status_failed(st))
    {
        mel_log_error("audiopolicy", "apply failed (status 0x%x)", st);
        return st;
    }

    g.in_force = in_force;
    g.applied = true;

    u32 warn_bits = st & ~MEL_AUDIOPOLICY_SEVERITY_MASK;
    if (warn_bits != 0)
    {
        mel_log_warn("audiopolicy", "policy lowered: category=%s mode=%s bits=0x%x", mel_audiopolicy_category_name(in_force.category), mel_audiopolicy_mode_name(in_force.mode), warn_bits);
        return MEL_AUDIOPOLICY_WARNED | warn_bits;
    }
    return MEL_AUDIOPOLICY_OK;
}

Mel_AudioPolicy mel_audiopolicy_current(void)
{
    if (!g.initialized)
        return (Mel_AudioPolicy){ 0 };
    return g.in_force;
}

Mel_AudioPolicy_Status mel_audiopolicy_override_output(const mel_audiopolicy_output* port)
{
    assert(g.initialized);
    assert(port != NULL);
    if (!g.initialized || port == NULL)
        return MEL_AUDIOPOLICY_ERROR | MEL_AUDIOPOLICY_RESULT_UNSUPPORTED;
    const Mel_AudioPolicy_Backend* b = mel_audiopolicy__backend();
    if (!b->override_output)
    {
        mel_log_warn("audiopolicy", "output override not supported on this platform");
        return MEL_AUDIOPOLICY_WARNED | MEL_AUDIOPOLICY_WARN_OVERRIDE_IGNORED;
    }
    return b->override_output(port);
}

Mel_AudioPolicy_Status mel_audiopolicy_focus_request(Mel_AudioPolicy_Focus_Opt opt)
{
    assert(g.initialized);
    if (!g.initialized)
        return MEL_AUDIOPOLICY_ERROR | MEL_AUDIOPOLICY_RESULT_UNSUPPORTED;
    const Mel_AudioPolicy_Backend* b = mel_audiopolicy__backend();
    Mel_AudioPolicy_Status         st = b->focus_request ? b->focus_request(opt) : MEL_AUDIOPOLICY_OK;
    if (!mel_audiopolicy_status_failed(st))
        g.focus_held = true;
    return st;
}

void mel_audiopolicy_focus_abandon(void)
{
    assert(g.initialized);
    assert(g.focus_held);
    if (!g.initialized || !g.focus_held)
        return;
    g.focus_held = false;
    const Mel_AudioPolicy_Backend* b = mel_audiopolicy__backend();
    if (b->focus_abandon)
        b->focus_abandon();
}

Mel_AudioPolicy_Sub mel_audiopolicy_subscribe(Mel_Executor* exec, Mel_AudioPolicy_Event_Callback cb, void* user)
{
    if (!g.initialized || g.events == NULL)
    {
        mel_log_error("audiopolicy", "subscribe before init; no channel");
        return MEL_AUDIOPOLICY_SUB_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g.exec;
    Mel_Event_Sub sub = mel_event_subscribe_push(g.events, target, (Mel_Event_Callback)cb, user);
    return (Mel_AudioPolicy_Sub){ sub.handle };
}

void mel_audiopolicy_unsubscribe(Mel_AudioPolicy_Sub sub)
{
    if (!g.initialized || g.events == NULL)
        return;
    mel_event_unsubscribe(g.events, (Mel_Event_Sub){ sub.handle });
}

void mel_audiopolicy__emit(const Mel_AudioPolicy_Event* ev)
{
    if (!g.initialized || g.events == NULL)
        return;
    if (ev->focus_lost && !ev->interruption_began)
        g.focus_held = false;
    if (ev->focus_gained)
        g.focus_held = true;
    mel_event_fire(g.events, ev);
}
