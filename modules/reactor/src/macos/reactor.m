#import <CoreFoundation/CoreFoundation.h>

#include <reactor/reactor.h>
#include <collection.array/array.h>
#include <allocator/allocator.h>
#include <allocator.heap/heap.h>
#include <core/compiler.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    MEL__REACTOR_KIND_INIT    = 0,
    MEL__REACTOR_KIND_EVENT   = 1,
    MEL__REACTOR_KIND_UPDATE  = 2,
    MEL__REACTOR_KIND_DESTROY = 3,
    MEL__REACTOR_KIND_COUNT   = 4,
};

typedef struct {
    void* fn;
    void* user;
    u32   generation;
    bool  alive;
} Mel_Reactor_Listener_Slot;

typedef Mel_Array(Mel_Reactor_Listener_Slot) Mel_Reactor_Listener_Array;
typedef Mel_Array(void*)                     Mel_Reactor_Inbox;

typedef struct Mel_Reactor_Internal {
    Mel_Reactor                self;
    bool                       is_system;
    u32                        next_listener_gen;
    Mel_Reactor_Listener_Array listeners[MEL__REACTOR_KIND_COUNT];

    pthread_t                  thread;
    bool                       thread_set;
    CFRunLoopRef               run_loop;
    CFRunLoopSourceRef         wake_source;
    CFRunLoopObserverRef       tick_observer;
    Mel_Reactor_Inbox          inbox_write;
    Mel_Reactor_Inbox          inbox_read;
    _Atomic(bool)              inbox_lock;
} Mel_Reactor_Internal;

static _Atomic(Mel_Reactor_Internal*) mel__reactor_table[MEL_REACTOR_MAX];
static _Atomic(u32)                   mel__reactor_next_gen = 1;
static Mel_Reactor                    mel__reactor_system_handle;
static bool                           mel__reactor_initialized;
static MEL_THREAD_LOCAL Mel_Reactor   mel__reactor_tls_current;

static const Mel_Alloc* mel__reactor_alloc(void) { return mel_alloc_heap(); }

static inline void mel__inbox_lock(Mel_Reactor_Internal* r)
{
    while (atomic_exchange_explicit(&r->inbox_lock, true, memory_order_acquire)) { /* spin */ }
}

static inline void mel__inbox_unlock(Mel_Reactor_Internal* r)
{
    atomic_store_explicit(&r->inbox_lock, false, memory_order_release);
}

static Mel_Reactor_Internal* mel__lookup(Mel_Reactor handle)
{
    if (!mel__reactor_initialized) return NULL;
    if (handle.handle.index >= MEL_REACTOR_MAX) return NULL;
    Mel_Reactor_Internal* r = atomic_load_explicit(&mel__reactor_table[handle.handle.index], memory_order_acquire);
    if (r == NULL) return NULL;
    if (r->self.handle.generation != handle.handle.generation) return NULL;
    return r;
}

static Mel_Reactor mel__register(Mel_Reactor_Internal* r)
{
    for (u32 i = 0; i < MEL_REACTOR_MAX; i++) {
        Mel_Reactor_Internal* expected = NULL;
        if (atomic_compare_exchange_strong_explicit(&mel__reactor_table[i], &expected, r,
                                                    memory_order_acq_rel, memory_order_acquire)) {
            u32 gen = atomic_fetch_add_explicit(&mel__reactor_next_gen, 1, memory_order_relaxed);
            r->self.handle.index      = i;
            r->self.handle.generation = gen;
            return r->self;
        }
    }
    return MEL_REACTOR_NULL;
}

static void mel__unregister(Mel_Reactor handle)
{
    if (handle.handle.index >= MEL_REACTOR_MAX) return;
    atomic_store_explicit(&mel__reactor_table[handle.handle.index], NULL, memory_order_release);
}

static bool mel__owns_caller(Mel_Reactor_Internal* r)
{
    if (!r->thread_set) return false;
    return pthread_equal(r->thread, pthread_self()) != 0;
}

static void mel__dispatch(Mel_Reactor_Internal* r, u8 kind, void* message)
{
    Mel_Reactor_Listener_Array* arr = &r->listeners[kind];
    for (usize i = 0; i < arr->count; i++) {
        Mel_Reactor_Listener_Slot* s = &arr->items[i];
        if (!s->alive) continue;
        switch (kind) {
            case MEL__REACTOR_KIND_INIT:
                ((Mel_Reactor_Init_Fn)s->fn)(r->self, s->user);
                break;
            case MEL__REACTOR_KIND_EVENT:
                ((Mel_Reactor_Event_Fn)s->fn)(r->self, s->user, message);
                break;
            case MEL__REACTOR_KIND_UPDATE:
                ((Mel_Reactor_Update_Fn)s->fn)(r->self, s->user);
                break;
            case MEL__REACTOR_KIND_DESTROY:
                ((Mel_Reactor_Destroy_Fn)s->fn)(r->self, s->user);
                break;
            default: break;
        }
    }
}

static void mel__wake_perform(void* info)
{
    Mel_Reactor_Internal* r = (Mel_Reactor_Internal*)info;

    mel__inbox_lock(r);
    Mel_Reactor_Inbox tmp = r->inbox_read;
    r->inbox_read  = r->inbox_write;
    r->inbox_write = tmp;
    mel__inbox_unlock(r);

    for (usize i = 0; i < r->inbox_read.count; i++) {
        mel__dispatch(r, MEL__REACTOR_KIND_EVENT, r->inbox_read.items[i]);
    }
    mel_array_clear(&r->inbox_read);
}

static void mel__tick_observe(CFRunLoopObserverRef obs, CFRunLoopActivity act, void* info)
{
    (void)obs;
    (void)act;
    Mel_Reactor_Internal* r = (Mel_Reactor_Internal*)info;
    mel__dispatch(r, MEL__REACTOR_KIND_UPDATE, NULL);
}

static bool mel__install_loop_artifacts(Mel_Reactor_Internal* r, CFRunLoopRef loop)
{
    r->run_loop = loop;
    CFRetain(loop);

    CFRunLoopSourceContext src_ctx = {
        .version = 0,
        .info    = r,
        .perform = mel__wake_perform,
    };
    r->wake_source = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &src_ctx);
    if (!r->wake_source) return false;
    CFRunLoopAddSource(loop, r->wake_source, kCFRunLoopCommonModes);

    CFRunLoopObserverContext obs_ctx = {
        .version = 0,
        .info    = r,
    };
    r->tick_observer = CFRunLoopObserverCreate(
        kCFAllocatorDefault,
        kCFRunLoopBeforeWaiting,
        true, 0,
        mel__tick_observe,
        &obs_ctx);
    if (!r->tick_observer) return false;
    CFRunLoopAddObserver(loop, r->tick_observer, kCFRunLoopCommonModes);

    return true;
}

static void mel__remove_loop_artifacts(Mel_Reactor_Internal* r)
{
    if (r->tick_observer) {
        CFRunLoopRemoveObserver(r->run_loop, r->tick_observer, kCFRunLoopCommonModes);
        CFRelease(r->tick_observer);
        r->tick_observer = NULL;
    }
    if (r->wake_source) {
        CFRunLoopRemoveSource(r->run_loop, r->wake_source, kCFRunLoopCommonModes);
        CFRelease(r->wake_source);
        r->wake_source = NULL;
    }
    if (r->run_loop) {
        CFRelease(r->run_loop);
        r->run_loop = NULL;
    }
}

static Mel_Reactor_Internal* mel__new(bool is_system)
{
    const Mel_Alloc* alloc = mel__reactor_alloc();
    Mel_Reactor_Internal* r = (Mel_Reactor_Internal*)mel_alloc(alloc, sizeof(*r));
    if (!r) return NULL;
    *r = (Mel_Reactor_Internal){ .is_system = is_system, .next_listener_gen = 1 };
    for (u32 k = 0; k < MEL__REACTOR_KIND_COUNT; k++) {
        mel_array_init(&r->listeners[k], alloc);
    }
    mel_array_init(&r->inbox_write, alloc);
    mel_array_init(&r->inbox_read,  alloc);
    atomic_store_explicit(&r->inbox_lock, false, memory_order_relaxed);
    return r;
}

static void mel__free(Mel_Reactor_Internal* r)
{
    for (u32 k = 0; k < MEL__REACTOR_KIND_COUNT; k++) {
        mel_array_free(&r->listeners[k]);
    }
    mel_array_free(&r->inbox_write);
    mel_array_free(&r->inbox_read);
    mel_dealloc(mel__reactor_alloc(), r);
}

static Mel_Reactor_Listener mel__add_listener(Mel_Reactor handle, u8 kind, void* fn, void* user)
{
    Mel_Reactor_Internal* r = mel__lookup(handle);
    if (!r || fn == NULL) return MEL_REACTOR_LISTENER_NULL;
    if (!mel__owns_caller(r)) {
        fprintf(stderr, "[reactor] listener registration must happen on the reactor's own thread\n");
        abort();
    }

    Mel_Reactor_Listener_Array* arr = &r->listeners[kind];

    u32 idx = (u32)arr->count;
    for (usize i = 0; i < arr->count; i++) {
        if (!arr->items[i].alive) { idx = (u32)i; break; }
    }

    u32 gen = r->next_listener_gen++;
    Mel_Reactor_Listener_Slot slot = { .fn = fn, .user = user, .generation = gen, .alive = true };

    if (idx == arr->count) {
        mel_array_push(arr, slot);
    } else {
        arr->items[idx] = slot;
    }

    return (Mel_Reactor_Listener){
        .reactor    = handle,
        .kind       = kind,
        .index      = idx,
        .generation = gen,
    };
}

bool mel_reactor_init(void)
{
    if (mel__reactor_initialized) return true;

    for (u32 i = 0; i < MEL_REACTOR_MAX; i++) {
        atomic_store_explicit(&mel__reactor_table[i], NULL, memory_order_relaxed);
    }
    mel__reactor_initialized = true;

    Mel_Reactor_Internal* sys = mel__new(true);
    if (!sys) { mel__reactor_initialized = false; return false; }

    Mel_Reactor h = mel__register(sys);
    if (!mel_reactor_valid(h)) {
        mel__free(sys);
        mel__reactor_initialized = false;
        return false;
    }

    sys->thread     = pthread_self();
    sys->thread_set = true;
    if (!mel__install_loop_artifacts(sys, CFRunLoopGetCurrent())) {
        mel__remove_loop_artifacts(sys);
        mel__unregister(h);
        mel__free(sys);
        mel__reactor_initialized = false;
        return false;
    }

    mel__reactor_system_handle = h;
    mel__reactor_tls_current   = h;
    return true;
}

void mel_reactor_shutdown(void)
{
    if (!mel__reactor_initialized) return;

    Mel_Reactor_Internal* sys = mel__lookup(mel__reactor_system_handle);
    if (sys) {
        mel__remove_loop_artifacts(sys);
        mel__unregister(mel__reactor_system_handle);
        mel__free(sys);
    }

    mel__reactor_system_handle = MEL_REACTOR_NULL;
    mel__reactor_tls_current   = MEL_REACTOR_NULL;
    mel__reactor_initialized   = false;
}

Mel_Reactor mel_reactor_system(void)
{
    return mel__reactor_system_handle;
}

Mel_Reactor mel_reactor_create(void)
{
    fprintf(stderr, "[reactor] user reactors are not supported on macOS; only the system reactor exists\n");
    return MEL_REACTOR_NULL;
}

void mel_reactor_destroy(Mel_Reactor handle)
{
    Mel_Reactor_Internal* r = mel__lookup(handle);
    if (!r) return;
    if (r->is_system) {
        fprintf(stderr, "[reactor] cannot destroy the system reactor; use mel_reactor_shutdown\n");
        return;
    }
    mel__remove_loop_artifacts(r);
    mel__unregister(handle);
    mel__free(r);
}

Mel_Reactor mel_reactor_current(void)
{
    return mel__reactor_tls_current;
}

bool mel_reactor_is_on(Mel_Reactor handle)
{
    Mel_Reactor_Internal* r = mel__lookup(handle);
    if (!r) return false;
    return mel__owns_caller(r);
}

void mel_reactor_assert_on(Mel_Reactor handle)
{
    if (mel_reactor_is_on(handle)) return;
    fprintf(stderr, "[reactor] call from wrong reactor thread; aborting\n");
    abort();
}

void mel_reactor_run(Mel_Reactor handle)
{
    Mel_Reactor_Internal* r = mel__lookup(handle);
    if (!r) return;
    if (!mel__owns_caller(r)) {
        fprintf(stderr, "[reactor] mel_reactor_run called from non-owning thread\n");
        abort();
    }
    if (r->run_loop != CFRunLoopGetCurrent()) {
        fprintf(stderr, "[reactor] run invoked from a thread that does not own this reactor\n");
        abort();
    }

    Mel_Reactor previous = mel__reactor_tls_current;
    mel__reactor_tls_current = handle;

    mel__dispatch(r, MEL__REACTOR_KIND_INIT, NULL);
    CFRunLoopRun();
    mel__dispatch(r, MEL__REACTOR_KIND_DESTROY, NULL);

    mel__reactor_tls_current = previous;
}

void mel_reactor_stop(Mel_Reactor handle)
{
    Mel_Reactor_Internal* r = mel__lookup(handle);
    if (!r || !r->run_loop) return;
    CFRunLoopStop(r->run_loop);
    CFRunLoopWakeUp(r->run_loop);
}

void mel_reactor_post(Mel_Reactor handle, void* message)
{
    Mel_Reactor_Internal* r = mel__lookup(handle);
    if (!r) return;

    mel__inbox_lock(r);
    mel_array_push(&r->inbox_write, message);
    mel__inbox_unlock(r);

    if (r->wake_source) CFRunLoopSourceSignal(r->wake_source);
    if (r->run_loop)    CFRunLoopWakeUp(r->run_loop);
}

Mel_Reactor_Listener mel_reactor_on_init(Mel_Reactor h, Mel_Reactor_Init_Fn fn, void* user)
{
    return mel__add_listener(h, MEL__REACTOR_KIND_INIT, (void*)fn, user);
}

Mel_Reactor_Listener mel_reactor_on_event(Mel_Reactor h, Mel_Reactor_Event_Fn fn, void* user)
{
    return mel__add_listener(h, MEL__REACTOR_KIND_EVENT, (void*)fn, user);
}

Mel_Reactor_Listener mel_reactor_on_update(Mel_Reactor h, Mel_Reactor_Update_Fn fn, void* user)
{
    return mel__add_listener(h, MEL__REACTOR_KIND_UPDATE, (void*)fn, user);
}

Mel_Reactor_Listener mel_reactor_on_destroy(Mel_Reactor h, Mel_Reactor_Destroy_Fn fn, void* user)
{
    return mel__add_listener(h, MEL__REACTOR_KIND_DESTROY, (void*)fn, user);
}

void mel_reactor_off(Mel_Reactor_Listener l)
{
    Mel_Reactor_Internal* r = mel__lookup(l.reactor);
    if (!r) return;
    if (!mel__owns_caller(r)) {
        fprintf(stderr, "[reactor] mel_reactor_off must be called on the reactor's own thread\n");
        abort();
    }
    if (l.kind >= MEL__REACTOR_KIND_COUNT) return;
    Mel_Reactor_Listener_Array* arr = &r->listeners[l.kind];
    if (l.index >= arr->count) return;
    if (arr->items[l.index].generation != l.generation) return;
    arr->items[l.index].alive = false;
}
