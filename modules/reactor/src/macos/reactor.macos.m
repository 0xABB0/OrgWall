#import <CoreFoundation/CoreFoundation.h>

#include "../reactor.internal.h"
#include <collection.array/array.h>
#include <allocator/allocator.h>
#include <allocator.heap/heap.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

typedef Mel_Array(void*) Mel_Reactor_Inbox;

typedef struct Mel_Reactor_Backend {
    pthread_t              thread;
    bool                   thread_set;
    CFRunLoopRef           run_loop;
    CFRunLoopSourceRef     wake_source;
    CFRunLoopObserverRef   tick_observer;

    Mel_Reactor_Inbox      inbox_write;
    Mel_Reactor_Inbox      inbox_read;
    _Atomic(bool)          inbox_lock;
} Mel_Reactor_Backend;

static inline void mel__inbox_lock(Mel_Reactor_Backend* b)
{
    while (atomic_exchange_explicit(&b->inbox_lock, true, memory_order_acquire)) { /* spin */ }
}

static inline void mel__inbox_unlock(Mel_Reactor_Backend* b)
{
    atomic_store_explicit(&b->inbox_lock, false, memory_order_release);
}

static void mel__reactor_wake_perform(void* info)
{
    Mel_Reactor_Internal* r = (Mel_Reactor_Internal*)info;
    Mel_Reactor_Backend*  b = r->backend;

    mel__inbox_lock(b);
    Mel_Reactor_Inbox tmp = b->inbox_read;
    b->inbox_read  = b->inbox_write;
    b->inbox_write = tmp;
    mel__inbox_unlock(b);

    for (usize i = 0; i < b->inbox_read.count; i++) {
        mel__reactor_dispatch_event(r, b->inbox_read.items[i]);
    }
    mel_array_clear(&b->inbox_read);
}

static void mel__reactor_tick_observe(CFRunLoopObserverRef obs, CFRunLoopActivity act, void* info)
{
    (void)obs;
    (void)act;
    Mel_Reactor_Internal* r = (Mel_Reactor_Internal*)info;
    mel__reactor_dispatch_update(r);
}

static bool mel__backend_install_loop_artifacts(Mel_Reactor_Internal* r, CFRunLoopRef loop)
{
    Mel_Reactor_Backend* b = r->backend;
    b->run_loop = loop;
    CFRetain(loop);

    CFRunLoopSourceContext src_ctx = {
        .version         = 0,
        .info            = r,
        .retain          = NULL,
        .release         = NULL,
        .copyDescription = NULL,
        .equal           = NULL,
        .hash            = NULL,
        .schedule        = NULL,
        .cancel          = NULL,
        .perform         = mel__reactor_wake_perform,
    };
    b->wake_source = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &src_ctx);
    if (!b->wake_source) return false;
    CFRunLoopAddSource(loop, b->wake_source, kCFRunLoopCommonModes);

    CFRunLoopObserverContext obs_ctx = {
        .version         = 0,
        .info            = r,
        .retain          = NULL,
        .release         = NULL,
        .copyDescription = NULL,
    };
    b->tick_observer = CFRunLoopObserverCreate(
        kCFAllocatorDefault,
        kCFRunLoopBeforeWaiting,
        true, 0,
        mel__reactor_tick_observe,
        &obs_ctx);
    if (!b->tick_observer) return false;
    CFRunLoopAddObserver(loop, b->tick_observer, kCFRunLoopCommonModes);

    return true;
}

static void mel__backend_remove_loop_artifacts(Mel_Reactor_Internal* r)
{
    Mel_Reactor_Backend* b = r->backend;
    if (b->tick_observer) {
        CFRunLoopRemoveObserver(b->run_loop, b->tick_observer, kCFRunLoopCommonModes);
        CFRelease(b->tick_observer);
        b->tick_observer = NULL;
    }
    if (b->wake_source) {
        CFRunLoopRemoveSource(b->run_loop, b->wake_source, kCFRunLoopCommonModes);
        CFRelease(b->wake_source);
        b->wake_source = NULL;
    }
    if (b->run_loop) {
        CFRelease(b->run_loop);
        b->run_loop = NULL;
    }
}

static Mel_Reactor_Backend* mel__backend_alloc(Mel_Reactor_Internal* r)
{
    Mel_Reactor_Backend* b = (Mel_Reactor_Backend*)calloc(1, sizeof(*b));
    if (!b) return NULL;
    mel_array_init(&b->inbox_write, mel_alloc_heap());
    mel_array_init(&b->inbox_read,  mel_alloc_heap());
    atomic_store_explicit(&b->inbox_lock, false, memory_order_relaxed);
    r->backend = b;
    return b;
}

static void mel__backend_free(Mel_Reactor_Internal* r)
{
    if (!r->backend) return;
    mel_array_free(&r->backend->inbox_write);
    mel_array_free(&r->backend->inbox_read);
    free(r->backend);
    r->backend = NULL;
}

bool mel__reactor_backend_adopt_system(Mel_Reactor_Internal* r)
{
    Mel_Reactor_Backend* b = mel__backend_alloc(r);
    if (!b) return false;
    b->thread     = pthread_self();
    b->thread_set = true;
    if (!mel__backend_install_loop_artifacts(r, CFRunLoopGetCurrent())) {
        mel__backend_remove_loop_artifacts(r);
        mel__backend_free(r);
        return false;
    }
    return true;
}

void mel__reactor_backend_release_system(Mel_Reactor_Internal* r)
{
    if (!r->backend) return;
    mel__backend_remove_loop_artifacts(r);
    mel__backend_free(r);
}

bool mel__reactor_backend_create_user(Mel_Reactor_Internal* r)
{
    (void)r;
    fprintf(stderr, "[reactor] user reactors are not supported on macOS; only the system reactor exists\n");
    return false;
}

void mel__reactor_backend_destroy_user(Mel_Reactor_Internal* r)
{
    (void)r;
}

void mel__reactor_backend_run(Mel_Reactor_Internal* r)
{
    if (!r->backend || !r->backend->run_loop) return;
    if (r->backend->run_loop != CFRunLoopGetCurrent()) {
        fprintf(stderr, "[reactor] run invoked from a thread that does not own this reactor\n");
        abort();
    }
    CFRunLoopRun();
}

void mel__reactor_backend_stop(Mel_Reactor_Internal* r)
{
    if (!r->backend || !r->backend->run_loop) return;
    CFRunLoopStop(r->backend->run_loop);
    CFRunLoopWakeUp(r->backend->run_loop);
}

void mel__reactor_backend_post(Mel_Reactor_Internal* r, void* message)
{
    if (!r->backend) return;
    Mel_Reactor_Backend* b = r->backend;

    mel__inbox_lock(b);
    mel_array_push(&b->inbox_write, message);
    mel__inbox_unlock(b);

    if (b->wake_source) CFRunLoopSourceSignal(b->wake_source);
    if (b->run_loop)    CFRunLoopWakeUp(b->run_loop);
}

bool mel__reactor_backend_owns_caller(Mel_Reactor_Internal* r)
{
    if (!r->backend || !r->backend->thread_set) return false;
    return pthread_equal(r->backend->thread, pthread_self()) != 0;
}
