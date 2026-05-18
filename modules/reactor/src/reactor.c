#include "reactor.internal.h"

#include <allocator/allocator.h>
#include <allocator.heap/heap.h>
#include <collection.slotmap/slotmap.fwd.h>
#include <core/compiler.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic(Mel_Reactor_Internal*) mel__reactor_table[MEL_REACTOR_MAX];
static _Atomic(u32)                   mel__reactor_next_gen = 1;
static Mel_Reactor                    mel__reactor_system_handle;
static bool                           mel__reactor_initialized;

static MEL_THREAD_LOCAL Mel_Reactor   mel__reactor_tls_current;

static const Mel_Alloc* mel__reactor_alloc(void) { return mel_alloc_heap(); }

Mel_Reactor_Internal* mel__reactor_lookup(Mel_Reactor handle)
{
    if (!mel__reactor_initialized) return NULL;
    if (handle.handle.index >= MEL_REACTOR_MAX) return NULL;
    Mel_Reactor_Internal* r = atomic_load_explicit(&mel__reactor_table[handle.handle.index], memory_order_acquire);
    if (r == NULL) return NULL;
    if (r->self.handle.generation != handle.handle.generation) return NULL;
    return r;
}

static Mel_Reactor mel__reactor_register(Mel_Reactor_Internal* r)
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

static void mel__reactor_unregister(Mel_Reactor handle)
{
    if (handle.handle.index >= MEL_REACTOR_MAX) return;
    atomic_store_explicit(&mel__reactor_table[handle.handle.index], NULL, memory_order_release);
}

static Mel_Reactor_Internal* mel__reactor_new(bool is_system)
{
    const Mel_Alloc* alloc = mel__reactor_alloc();
    Mel_Reactor_Internal* r = (Mel_Reactor_Internal*)mel_alloc(alloc, sizeof(*r));
    if (!r) return NULL;
    *r = (Mel_Reactor_Internal){ .is_system = is_system, .next_listener_gen = 1 };
    for (u32 k = 0; k < MEL__REACTOR_KIND_COUNT; k++) {
        mel_array_init(&r->listeners[k], alloc);
    }
    return r;
}

static void mel__reactor_free(Mel_Reactor_Internal* r)
{
    for (u32 k = 0; k < MEL__REACTOR_KIND_COUNT; k++) {
        mel_array_free(&r->listeners[k]);
    }
    mel_dealloc(mel__reactor_alloc(), r);
}

static Mel_Reactor_Listener mel__reactor_add_listener(Mel_Reactor handle, u8 kind, void* fn, void* user)
{
    Mel_Reactor_Internal* r = mel__reactor_lookup(handle);
    if (!r || fn == NULL) return MEL_REACTOR_LISTENER_NULL;
    if (!mel__reactor_backend_owns_caller(r)) {
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

static void mel__reactor_dispatch(Mel_Reactor_Internal* r, u8 kind, void* message)
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

void mel__reactor_dispatch_init   (Mel_Reactor_Internal* r) { mel__reactor_dispatch(r, MEL__REACTOR_KIND_INIT,    NULL); }
void mel__reactor_dispatch_event  (Mel_Reactor_Internal* r, void* m) { mel__reactor_dispatch(r, MEL__REACTOR_KIND_EVENT,  m); }
void mel__reactor_dispatch_update (Mel_Reactor_Internal* r) { mel__reactor_dispatch(r, MEL__REACTOR_KIND_UPDATE,  NULL); }
void mel__reactor_dispatch_destroy(Mel_Reactor_Internal* r) { mel__reactor_dispatch(r, MEL__REACTOR_KIND_DESTROY, NULL); }

bool mel_reactor_init(void)
{
    if (mel__reactor_initialized) return true;

    for (u32 i = 0; i < MEL_REACTOR_MAX; i++) {
        atomic_store_explicit(&mel__reactor_table[i], NULL, memory_order_relaxed);
    }
    mel__reactor_initialized = true;

    Mel_Reactor_Internal* sys = mel__reactor_new(true);
    if (!sys) { mel__reactor_initialized = false; return false; }

    Mel_Reactor h = mel__reactor_register(sys);
    if (!mel_reactor_valid(h)) {
        mel__reactor_free(sys);
        mel__reactor_initialized = false;
        return false;
    }

    if (!mel__reactor_backend_adopt_system(sys)) {
        mel__reactor_unregister(h);
        mel__reactor_free(sys);
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

    Mel_Reactor_Internal* sys = mel__reactor_lookup(mel__reactor_system_handle);
    if (sys) {
        mel__reactor_backend_release_system(sys);
        mel__reactor_unregister(mel__reactor_system_handle);
        mel__reactor_free(sys);
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
    if (!mel__reactor_initialized) return MEL_REACTOR_NULL;

    Mel_Reactor_Internal* r = mel__reactor_new(false);
    if (!r) return MEL_REACTOR_NULL;

    Mel_Reactor h = mel__reactor_register(r);
    if (!mel_reactor_valid(h)) { mel__reactor_free(r); return MEL_REACTOR_NULL; }

    if (!mel__reactor_backend_create_user(r)) {
        mel__reactor_unregister(h);
        mel__reactor_free(r);
        return MEL_REACTOR_NULL;
    }
    return h;
}

void mel_reactor_destroy(Mel_Reactor handle)
{
    Mel_Reactor_Internal* r = mel__reactor_lookup(handle);
    if (!r) return;
    if (r->is_system) {
        fprintf(stderr, "[reactor] cannot destroy the system reactor; use mel_reactor_shutdown\n");
        return;
    }
    mel__reactor_backend_destroy_user(r);
    mel__reactor_unregister(handle);
    mel__reactor_free(r);
}

Mel_Reactor mel_reactor_current(void)
{
    return mel__reactor_tls_current;
}

bool mel_reactor_is_on(Mel_Reactor handle)
{
    Mel_Reactor_Internal* r = mel__reactor_lookup(handle);
    if (!r) return false;
    return mel__reactor_backend_owns_caller(r);
}

void mel_reactor_assert_on(Mel_Reactor handle)
{
    if (mel_reactor_is_on(handle)) return;
    fprintf(stderr, "[reactor] call from wrong reactor thread; aborting\n");
    abort();
}

void mel_reactor_run(Mel_Reactor handle)
{
    Mel_Reactor_Internal* r = mel__reactor_lookup(handle);
    if (!r) return;
    if (!mel__reactor_backend_owns_caller(r)) {
        fprintf(stderr, "[reactor] mel_reactor_run called from non-owning thread\n");
        abort();
    }

    Mel_Reactor previous = mel__reactor_tls_current;
    mel__reactor_tls_current = handle;

    mel__reactor_dispatch_init(r);
    mel__reactor_backend_run(r);
    mel__reactor_dispatch_destroy(r);

    mel__reactor_tls_current = previous;
}

void mel_reactor_stop(Mel_Reactor handle)
{
    Mel_Reactor_Internal* r = mel__reactor_lookup(handle);
    if (!r) return;
    mel__reactor_backend_stop(r);
}

void mel_reactor_post(Mel_Reactor handle, void* message)
{
    Mel_Reactor_Internal* r = mel__reactor_lookup(handle);
    if (!r) return;
    mel__reactor_backend_post(r, message);
}

Mel_Reactor_Listener mel_reactor_on_init(Mel_Reactor h, Mel_Reactor_Init_Fn fn, void* user)
{
    return mel__reactor_add_listener(h, MEL__REACTOR_KIND_INIT, (void*)fn, user);
}

Mel_Reactor_Listener mel_reactor_on_event(Mel_Reactor h, Mel_Reactor_Event_Fn fn, void* user)
{
    return mel__reactor_add_listener(h, MEL__REACTOR_KIND_EVENT, (void*)fn, user);
}

Mel_Reactor_Listener mel_reactor_on_update(Mel_Reactor h, Mel_Reactor_Update_Fn fn, void* user)
{
    return mel__reactor_add_listener(h, MEL__REACTOR_KIND_UPDATE, (void*)fn, user);
}

Mel_Reactor_Listener mel_reactor_on_destroy(Mel_Reactor h, Mel_Reactor_Destroy_Fn fn, void* user)
{
    return mel__reactor_add_listener(h, MEL__REACTOR_KIND_DESTROY, (void*)fn, user);
}

void mel_reactor_off(Mel_Reactor_Listener l)
{
    Mel_Reactor_Internal* r = mel__reactor_lookup(l.reactor);
    if (!r) return;
    if (!mel__reactor_backend_owns_caller(r)) {
        fprintf(stderr, "[reactor] mel_reactor_off must be called on the reactor's own thread\n");
        abort();
    }
    if (l.kind >= MEL__REACTOR_KIND_COUNT) return;
    Mel_Reactor_Listener_Array* arr = &r->listeners[l.kind];
    if (l.index >= arr->count) return;
    if (arr->items[l.index].generation != l.generation) return;
    arr->items[l.index].alive = false;
}
