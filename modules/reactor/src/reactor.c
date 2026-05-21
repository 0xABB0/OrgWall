#include <reactor/reactor.h>

#include <core/types.h>
#include <core/platform.h>
#include <time/nano.h>

#include <stdatomic.h>
#include <stdlib.h>

#define MEL_REACTOR_MAX_POLLS 64

struct Mel_Reactor {
    Mel_Reactor_Source* sources;
    Mel_Reactor_Poll*   poll_set[MEL_REACTOR_MAX_POLLS];
    int                 iterating;
    bool                needs_reap;
    atomic_bool         running;
#if MEL_PLATFORM_WINDOWS
    u32 win32_thread;
#elif MEL_PLATFORM_APPLE
    void* cf_loop;
    void* cf_wake;
#elif MEL_PLATFORM_ANDROID
    void* looper;
    int   timer_fd;
    bool  android_looping;
#elif MEL_PLATFORM_WEB
    bool web_looping;
#elif MEL_PLATFORM_POSIX
    int wake_pipe[2];
#else
    #error "reactor: no backend for this platform"
#endif
};

#if MEL_PLATFORM_WINDOWS
    #include "win32/reactor_backend.inl"
#elif MEL_PLATFORM_APPLE
    #include "apple/reactor_backend.inl"
#elif MEL_PLATFORM_ANDROID
    #include "android/reactor_backend.inl"
#elif MEL_PLATFORM_WEB
    #include "web/reactor_backend.inl"
#elif MEL_PLATFORM_POSIX
    #include "posix/reactor_backend.inl"
#endif

static Mel_Reactor g_system;
static bool        g_system_ready;

static i32 reactor_fold_timeout(i32 a, i32 b)
{
    if (a < 0) return b;
    if (b < 0) return a;
    return a < b ? a : b;
}

static void reactor_source_unlink(Mel_Reactor_Source* s)
{
    Mel_Reactor* r = s->reactor;
    if (!r) return;
    if (s->prev) s->prev->next = s->next;
    else         r->sources    = s->next;
    if (s->next) s->next->prev = s->prev;
    s->prev     = NULL;
    s->next     = NULL;
    s->reactor  = NULL;
    s->attached = false;
    s->ready    = false;
}

static void reactor_source_dispose(Mel_Reactor_Source* s)
{
    if (s->cb.finalize) s->cb.finalize(s);
    reactor_source_unlink(s);
    free(s->polls);
    free(s);
}

static void reactor_reap(Mel_Reactor* r)
{
    Mel_Reactor_Source* s = r->sources;
    while (s) {
        Mel_Reactor_Source* next = s->next;
        if (s->destroyed) {
            reactor_source_dispose(s);
        } else if (s->detach_pending) {
            s->detach_pending = false;
            reactor_source_unlink(s);
        }
        s = next;
    }
}

bool mel_reactor_init(void)
{
    if (g_system_ready) return true;
    g_system.sources    = NULL;
    g_system.iterating  = 0;
    g_system.needs_reap = false;
    atomic_store(&g_system.running, false);
    if (!reactor_backend_init(&g_system)) return false;
    g_system_ready = true;
    return true;
}

void mel_reactor_shutdown(void)
{
    if (!g_system_ready) return;
    while (g_system.sources) {
        Mel_Reactor_Source* s = g_system.sources;
        s->destroyed = true;
        reactor_source_dispose(s);
    }
    reactor_backend_shutdown(&g_system);
    g_system_ready = false;
}

Mel_Reactor* mel_reactor_system(void)
{
    return g_system_ready ? &g_system : NULL;
}

void mel_reactor_run(Mel_Reactor* r)
{
    if (!r) return;
    atomic_store(&r->running, true);
#if MEL_PLATFORM_WEB || MEL_PLATFORM_ANDROID
    reactor_backend_run(r);
#else
    while (mel_reactor_iterate(r, true)) { }
#endif
}

void mel_reactor_quit(Mel_Reactor* r)
{
    if (!r) return;
    atomic_store(&r->running, false);
    reactor_backend_wake(r);
}

void mel_reactor_wake(Mel_Reactor* r)
{
    if (r) reactor_backend_wake(r);
}

bool mel_reactor_is_running(Mel_Reactor* r)
{
    return r && atomic_load(&r->running);
}

bool mel_reactor_iterate(Mel_Reactor* r, bool may_block)
{
    if (!r) return false;

    r->iterating++;

    i32   timeout    = MEL_REACTOR_FOREVER;
    bool  any_ready  = false;
    usize poll_count = 0;
    for (Mel_Reactor_Source* s = r->sources; s; s = s->next) {
        if (s->destroyed) continue;
        bool ready;
        if (s->flags & MEL_REACTOR_SOURCE_READY) {
            ready = true;
        } else if (s->cb.prepare) {
            i32 src_timeout = MEL_REACTOR_FOREVER;
            ready   = s->cb.prepare(s, &src_timeout);
            timeout = reactor_fold_timeout(timeout, src_timeout);
        } else {
            ready = false;
        }
        if (ready) {
            s->ready  = true;
            any_ready = true;
        }
        for (usize i = 0; i < s->poll_count && poll_count < MEL_REACTOR_MAX_POLLS; i++) {
            r->poll_set[poll_count++] = s->polls[i];
        }
    }
    if (any_ready || !may_block) timeout = MEL_REACTOR_NOWAIT;

    bool keep = reactor_backend_wait(r, r->poll_set, poll_count, timeout);
    if (!keep) atomic_store(&r->running, false);

    for (Mel_Reactor_Source* s = r->sources; s; ) {
        Mel_Reactor_Source* next = s->next;
        if (!s->destroyed) {
            bool ready = s->ready;
            if (!ready && s->cb.check) ready = s->cb.check(s);
            if (ready) {
                s->ready = false;
                bool live;
                if (s->cb.dispatch) {
                    live = s->cb.dispatch(s, s->callback, s->user);
                } else {
                    live = s->callback ? s->callback(s->user) : true;
                }
                if (!live) mel_reactor_source_destroy(s);
            }
        }
        s = next;
    }

    r->iterating--;
    if (r->iterating == 0 && r->needs_reap) {
        r->needs_reap = false;
        reactor_reap(r);
    }

    return atomic_load(&r->running);
}

Mel_Reactor_Source* mel_reactor_source_new(const Mel_Reactor_Source_Callbacks* cb, usize struct_size)
{
    usize size = struct_size < sizeof(Mel_Reactor_Source) ? sizeof(Mel_Reactor_Source) : struct_size;
    Mel_Reactor_Source* s = calloc(1, size);
    if (!s) return NULL;
    if (cb) s->cb = *cb;
    return s;
}

void mel_reactor_source_attach(Mel_Reactor* r, Mel_Reactor_Source* s)
{
    if (!r || !s || s->attached) return;
    s->reactor  = r;
    s->attached = true;
    s->prev     = NULL;
    s->next     = r->sources;
    if (r->sources) r->sources->prev = s;
    r->sources = s;
    reactor_backend_wake(r);
}

void mel_reactor_source_detach(Mel_Reactor_Source* s)
{
    if (!s || !s->reactor) return;
    if (s->reactor->iterating > 0) {
        s->detach_pending      = true;
        s->reactor->needs_reap = true;
        return;
    }
    reactor_source_unlink(s);
}

void mel_reactor_source_destroy(Mel_Reactor_Source* s)
{
    if (!s || s->destroyed) return;
    s->destroyed = true;
    if (s->reactor && s->reactor->iterating > 0) {
        s->reactor->needs_reap = true;
        return;
    }
    reactor_source_dispose(s);
}

void mel_reactor_source_set_callback(Mel_Reactor_Source* s, Mel_Reactor_Source_Proc callback, void* user)
{
    if (!s) return;
    s->callback = callback;
    s->user     = user;
}

Mel_Reactor* mel_reactor_source_reactor(const Mel_Reactor_Source* s)
{
    return s ? s->reactor : NULL;
}

void mel_reactor_source_add_poll(Mel_Reactor_Source* s, Mel_Reactor_Poll* poll)
{
    if (!s || !poll) return;
    if (s->poll_count == s->poll_cap) {
        usize cap = s->poll_cap ? s->poll_cap * 2 : 4;
        Mel_Reactor_Poll** grown = realloc(s->polls, cap * sizeof *grown);
        if (!grown) return;
        s->polls    = grown;
        s->poll_cap = cap;
    }
    s->polls[s->poll_count++] = poll;
}

void mel_reactor_source_remove_poll(Mel_Reactor_Source* s, Mel_Reactor_Poll* poll)
{
    if (!s) return;
    for (usize i = 0; i < s->poll_count; i++) {
        if (s->polls[i] == poll) {
            s->polls[i] = s->polls[--s->poll_count];
            return;
        }
    }
}

Mel_Reactor_Source* mel_reactor_idle_new(Mel_Reactor_Source_Proc callback, void* user)
{
    Mel_Reactor_Source* s = mel_reactor_source_new(NULL, sizeof(Mel_Reactor_Source));
    if (!s) return NULL;
    s->flags = MEL_REACTOR_SOURCE_READY;
    mel_reactor_source_set_callback(s, callback, user);
    return s;
}

typedef struct {
    Mel_Reactor_Source base;
    i64                interval_ns;
    u64                next_fire_ns;
} Reactor_Timer;

static bool reactor_timer_prepare(Mel_Reactor_Source* source, i32* timeout)
{
    Reactor_Timer* t   = (Reactor_Timer*)source;
    u64            now = mel_nanos_since_unspecified_epoch();
    if (now >= t->next_fire_ns) {
        *timeout = MEL_REACTOR_NOWAIT;
        return true;
    }
    u64 ms = (t->next_fire_ns - now + 999999u) / 1000000u;
    if (ms > 0x7fffffffu) ms = 0x7fffffffu;
    *timeout = (i32)ms;
    return false;
}

static bool reactor_timer_check(Mel_Reactor_Source* source)
{
    Reactor_Timer* t = (Reactor_Timer*)source;
    return mel_nanos_since_unspecified_epoch() >= t->next_fire_ns;
}

static bool reactor_timer_dispatch(Mel_Reactor_Source* source, Mel_Reactor_Source_Proc callback, void* user)
{
    Reactor_Timer* t   = (Reactor_Timer*)source;
    u64            now = mel_nanos_since_unspecified_epoch();
    if (t->interval_ns > 0) {
        t->next_fire_ns += (u64)t->interval_ns;
        if (t->next_fire_ns <= now) t->next_fire_ns = now + (u64)t->interval_ns;
    } else {
        t->next_fire_ns = now;
    }
    return callback ? callback(user) : true;
}

Mel_Reactor_Source* mel_reactor_timer_new(i64 interval_ns, Mel_Reactor_Source_Proc callback, void* user)
{
    static const Mel_Reactor_Source_Callbacks vt = {
        .prepare  = reactor_timer_prepare,
        .check    = reactor_timer_check,
        .dispatch = reactor_timer_dispatch,
    };
    Mel_Reactor_Source* s = mel_reactor_source_new(&vt, sizeof(Reactor_Timer));
    if (!s) return NULL;
    Reactor_Timer* t = (Reactor_Timer*)s;
    t->interval_ns  = interval_ns > 0 ? interval_ns : 0;
    t->next_fire_ns = mel_nanos_since_unspecified_epoch() + (u64)t->interval_ns;
    mel_reactor_source_set_callback(s, callback, user);
    return s;
}

void mel_reactor_timer_set_interval(Mel_Reactor_Source* s, i64 interval_ns)
{
    if (!s) return;
    Reactor_Timer* t = (Reactor_Timer*)s;
    t->interval_ns  = interval_ns > 0 ? interval_ns : 0;
    t->next_fire_ns = mel_nanos_since_unspecified_epoch() + (u64)t->interval_ns;
    if (s->reactor) mel_reactor_wake(s->reactor);
}
