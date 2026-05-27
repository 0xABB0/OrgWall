#include <reactor/reactor.h>

#include <core/types.h>
#include <core/platform.h>
#include <time/nano.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.mpsc/mpsc.h>
#include <thread/thread.h>

#include <stdatomic.h>
#include <string.h>

#ifdef _CLANGD
#pragma once
#endif

#define MEL_REACTOR_MAX_POLLS        64
#define MEL_REACTOR_PRIORITY_BUCKETS  8

typedef struct {
    Mel_Mpsc_Node         node;
    Mel_Reactor_Post_Proc cb;
    void*                 user;
} Reactor_Post;

struct Mel_Reactor {
    Mel_Reactor_Mode    mode;
    const Mel_Alloc*    alloc;
    const Mel_Alloc*    post_alloc;

    Mel_Reactor_Init_Proc init;
    void*                 init_user;
    bool                  init_done;

    Mel_Thread_Id       owner;
    bool                has_owner;

    Mel_Reactor_Source* sources;
    Mel_Reactor_Poll*   poll_set[MEL_REACTOR_MAX_POLLS];

    Mel_Mpsc            posts;

    int                 iterating;
    bool                needs_reap;
    atomic_bool         running;

#if MEL_PLATFORM_WINDOWS
#elif MEL_PLATFORM_APPLE
    void* cf_loop;
    void* cf_wake;
    void* cf_tick;
    void* cf_fd_polls[MEL_REACTOR_MAX_POLLS];
    void* cf_fd_refs[MEL_REACTOR_MAX_POLLS];
    void* cf_fd_srcs[MEL_REACTOR_MAX_POLLS];
    usize cf_fd_count;
#elif MEL_PLATFORM_ANDROID
    void* looper;
    int   timer_fd;
    int   reg_fds[MEL_REACTOR_MAX_POLLS];
    usize reg_fd_count;
    bool  android_looping;
#elif MEL_PLATFORM_WEB
    long web_id;
    int  web_kind;
#elif MEL_PLATFORM_POSIX
    int wake_pipe[2];
#else
    #error "reactor: no backend for this platform"
#endif
};

static bool reactor_iterate                          (Mel_Reactor* r, bool may_block);
[[maybe_unused]] static void reactor_attached_destroy(Mel_Reactor* r);

#if MEL_PLATFORM_WINDOWS
    #include "win32/reactor_backend.inl"
#elif MEL_PLATFORM_APPLE
    #include "apple/reactor_backend.inl"
#elif MEL_PLATFORM_ANDROID
    #include "android/reactor_backend.inl"
#elif MEL_PLATFORM_EMSCRIPTEN
    #include "web/reactor_backend.inl"
#elif MEL_PLATFORM_WASI
    #include "wasi/reactor_backend.inl"
#elif MEL_PLATFORM_POSIX
    #include "posix/reactor_backend.inl"
#endif

#ifndef MEL_REACTOR_BACKEND_HAS_ATTACHED
    #define MEL_REACTOR_BACKEND_HAS_ATTACHED 0
#endif

static void reactor_capture_owner(Mel_Reactor* r)
{
    if (r->has_owner) return;
    r->owner     = mel_thread_current_id();
    r->has_owner = true;
}

static i32 reactor_fold_timeout(i32 a, i32 b)
{
    if (a < 0) return b;
    if (b < 0) return a;
    return a < b ? a : b;
}

static i32 reactor_priority_bucket(i32 priority)
{
    i64 span    = (i64)MEL_REACTOR_PRIORITY_IDLE - (i64)MEL_REACTOR_PRIORITY_HIGH + 1;
    i64 shifted = (i64)priority - (i64)MEL_REACTOR_PRIORITY_HIGH;
    i64 b       = shifted * MEL_REACTOR_PRIORITY_BUCKETS / span;
    if (b < 0) b = 0;
    if (b >= MEL_REACTOR_PRIORITY_BUCKETS) b = MEL_REACTOR_PRIORITY_BUCKETS - 1;
    return (i32)b;
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
    const Mel_Alloc* alloc = s->reactor ? s->reactor->alloc : mel_alloc_heap();
    bool             ext   = s->external_storage;
    if (s->cb && s->cb->finalize) s->cb->finalize(s);
    reactor_source_unlink(s);
    if (s->polls) mel_dealloc(alloc, s->polls);
    if (!ext) mel_dealloc(alloc, s);
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

static void reactor_drain_posts(Mel_Reactor* r)
{
    for (;;) {
        Mel_Mpsc_Node* n = mel_mpsc_pop(&r->posts);
        if (!n) break;
        Reactor_Post* p = (Reactor_Post*)n;
        if (p->cb) p->cb(p->user);
        mel_dealloc(r->post_alloc, p);
    }
}

static void reactor_destroy_all_sources(Mel_Reactor* r)
{
    while (r->sources) {
        Mel_Reactor_Source* s = r->sources;
        s->destroyed = true;
        reactor_source_dispose(s);
    }
}

[[maybe_unused]] static void reactor_attached_destroy(Mel_Reactor* r)
{
    reactor_drain_posts(r);
    reactor_destroy_all_sources(r);
    reactor_backend_shutdown(r);
    const Mel_Alloc* alloc = r->alloc;
    mel_dealloc(alloc, r);
}

static bool reactor_iterate(Mel_Reactor* r, bool may_block)
{
    if (!r) return false;

    r->iterating++;
    reactor_capture_owner(r);

    reactor_drain_posts(r);

    i64   now        = (i64)mel_nanos_since_unspecified_epoch();
    i32   timeout    = MEL_REACTOR_FOREVER;
    bool  any_ready  = false;
    usize poll_count = 0;

    for (Mel_Reactor_Source* s = r->sources; s; s = s->next) {
        if (s->destroyed || s->detach_pending) continue;
        bool ready = false;
        if (s->ready_time >= 0) {
            if (now >= s->ready_time) {
                ready = true;
            } else {
                i64 delta = s->ready_time - now;
                i64 ms    = (delta + 999999) / 1000000;
                if (ms > 0x7fffffff) ms = 0x7fffffff;
                timeout = reactor_fold_timeout(timeout, (i32)ms);
            }
        }
        if (!ready && s->cb && s->cb->prepare) {
            i32 src_timeout = MEL_REACTOR_FOREVER;
            if (s->cb->prepare(s, &src_timeout)) ready = true;
            timeout = reactor_fold_timeout(timeout, src_timeout);
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

    reactor_drain_posts(r);

    for (i32 bucket = 0; bucket < MEL_REACTOR_PRIORITY_BUCKETS; bucket++) {
        Mel_Reactor_Source* s = r->sources;
        while (s) {
            Mel_Reactor_Source* next = s->next;
            if (s->destroyed || s->detach_pending) { s = next; continue; }
            if (reactor_priority_bucket(s->priority) != bucket) { s = next; continue; }
            bool ready = s->ready;
            if (!ready && s->cb && s->cb->check) ready = s->cb->check(s);
            if (ready) {
                s->ready = false;
                bool live;
                if (s->cb && s->cb->dispatch) {
                    live = s->cb->dispatch(s, s->callback, s->user);
                } else {
                    live = s->callback ? s->callback(s->user) : true;
                }
                if (!live) {
                    s->destroyed  = true;
                    r->needs_reap = true;
                }
            }
            s = next;
        }
    }

    r->iterating--;
    if (r->iterating == 0 && r->needs_reap) {
        r->needs_reap = false;
        reactor_reap(r);
    }

    return atomic_load(&r->running);
}

void mel_reactor_quit(Mel_Reactor* r)
{
    if (!r) return;
    atomic_store(&r->running, false);
    reactor_backend_wake(r);
}

bool mel_reactor_is_running(const Mel_Reactor* r)
{
    return r && atomic_load((atomic_bool*)&r->running);
}

bool mel_reactor_is_owner(const Mel_Reactor* r)
{
    if (!r || !r->has_owner) return false;
    return mel_thread_id_equal(r->owner, mel_thread_current_id());
}

void mel_reactor_post(Mel_Reactor* r, Mel_Reactor_Post_Proc cb, void* user)
{
    if (!r || !cb) return;
    if (mel_reactor_is_owner(r)) {
        cb(user);
        return;
    }
    Reactor_Post* p = mel_alloc(r->post_alloc, sizeof(Reactor_Post));
    if (!p) return;
    p->cb   = cb;
    p->user = user;
    mel_mpsc_push(&r->posts, &p->node);
    reactor_backend_wake(r);
}

static int reactor_run_threaded(Mel_Reactor* r)
{
    reactor_capture_owner(r);
    atomic_store(&r->running, true);
    if (r->init && !r->init(r, r->init_user)) {
        atomic_store(&r->running, false);
        return 1;
    }
    r->init_done = true;
    while (reactor_iterate(r, true)) { }
    return 0;
}

int mel_reactor_spawn_opt(Mel_Reactor_Mode mode, Mel_Reactor_Init_Proc init,
                          void* user, Mel_Reactor_Spawn_Opt opt)
{
    const Mel_Alloc* alloc      = opt.alloc      ? opt.alloc      : mel_alloc_heap();
    const Mel_Alloc* post_alloc = opt.post_alloc ? opt.post_alloc : alloc;

    Mel_Reactor* r = mel_calloc(alloc, sizeof(Mel_Reactor));
    if (!r) return 2;

    r->mode       = mode;
    r->alloc      = alloc;
    r->post_alloc = post_alloc;
    r->init       = init;
    r->init_user  = user;

    mel_mpsc_init(&r->posts);

    if (!reactor_backend_init(r)) {
        mel_dealloc(alloc, r);
        return 3;
    }

    switch (mode) {
        case MEL_REACTOR_THREADED: {
#if MEL_PLATFORM_EMSCRIPTEN
            // The browser owns the event loop; a blocking run would freeze the
            // tab. Drive the reactor from the main-loop backend (requestAnimation
            // Frame) and return to the browser, exactly like the attached mode.
            // wasi has no such constraint and uses the normal blocking run below.
            atomic_store(&r->running, true);
            reactor_backend_attached_run(r);
            return 0;
#else
            int rc = reactor_run_threaded(r);
            reactor_drain_posts(r);
            reactor_destroy_all_sources(r);
            reactor_backend_shutdown(r);
            mel_dealloc(alloc, r);
            return rc;
#endif
        }
        case MEL_REACTOR_ATTACHED: {
#if MEL_REACTOR_BACKEND_HAS_ATTACHED
            atomic_store(&r->running, true);
            reactor_backend_attached_run(r);
            return 0;
#else
            reactor_backend_shutdown(r);
            mel_dealloc(alloc, r);
            return 4;
#endif
        }
    }

    reactor_backend_shutdown(r);
    mel_dealloc(alloc, r);
    return 5;
}

Mel_Reactor_Source* mel_reactor_source_new(const Mel_Reactor_Source_Callbacks* cb, usize struct_size)
{
    if (struct_size < sizeof(Mel_Reactor_Source)) struct_size = sizeof(Mel_Reactor_Source);
    Mel_Reactor_Source* s = mel_calloc(mel_alloc_heap(), struct_size);
    if (!s) return NULL;
    s->cb         = cb;
    s->ready_time = MEL_REACTOR_READY_TIME_NEVER;
    s->priority   = MEL_REACTOR_PRIORITY_DEFAULT;
    return s;
}

void mel_reactor_source_init(Mel_Reactor_Source* s, const Mel_Reactor_Source_Callbacks* cb)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->cb               = cb;
    s->ready_time       = MEL_REACTOR_READY_TIME_NEVER;
    s->priority         = MEL_REACTOR_PRIORITY_DEFAULT;
    s->external_storage = true;
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

void mel_reactor_source_set_callback(Mel_Reactor_Source* s, Mel_Reactor_Source_Proc cb, void* user)
{
    if (!s) return;
    s->callback = cb;
    s->user     = user;
}

void mel_reactor_source_set_priority(Mel_Reactor_Source* s, i32 priority)
{
    if (s) s->priority = priority;
}

i32 mel_reactor_source_get_priority(const Mel_Reactor_Source* s)
{
    return s ? s->priority : MEL_REACTOR_PRIORITY_DEFAULT;
}

void mel_reactor_source_set_ready_time(Mel_Reactor_Source* s, i64 ns)
{
    if (!s) return;
    s->ready_time = ns;
    if (s->reactor) reactor_backend_wake(s->reactor);
}

i64 mel_reactor_source_get_ready_time(const Mel_Reactor_Source* s)
{
    return s ? s->ready_time : MEL_REACTOR_READY_TIME_NEVER;
}

Mel_Reactor* mel_reactor_source_reactor(const Mel_Reactor_Source* s)
{
    return s ? s->reactor : NULL;
}

void mel_reactor_source_add_poll(Mel_Reactor_Source* s, Mel_Reactor_Poll* p)
{
    if (!s || !p) return;
    const Mel_Alloc* alloc = s->reactor ? s->reactor->alloc : mel_alloc_heap();
    if (s->poll_count == s->poll_cap) {
        usize cap = s->poll_cap ? (usize)s->poll_cap * 2 : 4;
        Mel_Reactor_Poll** grown = s->polls
            ? mel_realloc(alloc, s->polls, cap * sizeof *grown)
            : mel_alloc  (alloc,           cap * sizeof *grown);
        if (!grown) return;
        s->polls    = grown;
        s->poll_cap = (u16)cap;
    }
    s->polls[s->poll_count++] = p;
}

void mel_reactor_source_remove_poll(Mel_Reactor_Source* s, Mel_Reactor_Poll* p)
{
    if (!s) return;
    for (u16 i = 0; i < s->poll_count; i++) {
        if (s->polls[i] == p) {
            s->polls[i] = s->polls[--s->poll_count];
            return;
        }
    }
}

static bool reactor_idle_dispatch(Mel_Reactor_Source* s, Mel_Reactor_Source_Proc cb, void* user)
{
    (void)s;
    return cb ? cb(user) : true;
}

Mel_Reactor_Source* mel_reactor_idle_new(Mel_Reactor_Source_Proc cb, void* user)
{
    static const Mel_Reactor_Source_Callbacks vt = { .dispatch = reactor_idle_dispatch };
    Mel_Reactor_Source* s = mel_reactor_source_new(&vt, sizeof(Mel_Reactor_Source));
    if (!s) return NULL;
    s->ready_time = MEL_REACTOR_READY_TIME_NOW;
    s->priority   = MEL_REACTOR_PRIORITY_IDLE;
    mel_reactor_source_set_callback(s, cb, user);
    return s;
}

typedef struct {
    Mel_Reactor_Source base;
    i64                interval_ns;
} Reactor_Timer;

static bool reactor_timer_dispatch(Mel_Reactor_Source* s, Mel_Reactor_Source_Proc cb, void* user)
{
    Reactor_Timer* t   = (Reactor_Timer*)s;
    i64            now = (i64)mel_nanos_since_unspecified_epoch();
    if (t->interval_ns > 0) {
        s->ready_time += t->interval_ns;
        if (s->ready_time <= now) s->ready_time = now + t->interval_ns;
    } else {
        s->ready_time = now;
    }
    return cb ? cb(user) : true;
}

Mel_Reactor_Source* mel_reactor_timer_new(i64 interval_ns, Mel_Reactor_Source_Proc cb, void* user)
{
    static const Mel_Reactor_Source_Callbacks vt = { .dispatch = reactor_timer_dispatch };
    Mel_Reactor_Source* s = mel_reactor_source_new(&vt, sizeof(Reactor_Timer));
    if (!s) return NULL;
    Reactor_Timer* t = (Reactor_Timer*)s;
    t->interval_ns   = interval_ns > 0 ? interval_ns : 0;
    s->ready_time    = (i64)mel_nanos_since_unspecified_epoch() + t->interval_ns;
    mel_reactor_source_set_callback(s, cb, user);
    return s;
}

void mel_reactor_timer_set_interval(Mel_Reactor_Source* s, i64 interval_ns)
{
    if (!s) return;
    Reactor_Timer* t = (Reactor_Timer*)s;
    t->interval_ns   = interval_ns > 0 ? interval_ns : 0;
    s->ready_time    = (i64)mel_nanos_since_unspecified_epoch() + t->interval_ns;
    if (s->reactor) reactor_backend_wake(s->reactor);
}
