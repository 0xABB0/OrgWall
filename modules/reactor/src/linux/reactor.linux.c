#include "../reactor.internal.h"

#include <collection.array/array.h>
#include <allocator/allocator.h>
#include <allocator.heap/heap.h>

#include <pthread.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

typedef Mel_Array(void*) Mel_Reactor_Inbox;

typedef struct Mel_Reactor_Backend {
    pthread_t          thread;
    bool               thread_set;
    int                epoll_fd;
    int                wake_fd;
    _Atomic(bool)      stop_requested;
    Mel_Reactor_Inbox  inbox_write;
    Mel_Reactor_Inbox  inbox_read;
    _Atomic(bool)      inbox_lock;
} Mel_Reactor_Backend;

static inline void mel__inbox_lock(Mel_Reactor_Backend* b)
{
    while (atomic_exchange_explicit(&b->inbox_lock, true, memory_order_acquire)) { /* spin */ }
}

static inline void mel__inbox_unlock(Mel_Reactor_Backend* b)
{
    atomic_store_explicit(&b->inbox_lock, false, memory_order_release);
}

static Mel_Reactor_Backend* mel__backend_alloc(Mel_Reactor_Internal* r)
{
    Mel_Reactor_Backend* b = (Mel_Reactor_Backend*)calloc(1, sizeof(*b));
    if (!b) return NULL;
    mel_array_init(&b->inbox_write, mel_alloc_heap());
    mel_array_init(&b->inbox_read,  mel_alloc_heap());
    atomic_store_explicit(&b->inbox_lock,      false, memory_order_relaxed);
    atomic_store_explicit(&b->stop_requested,  false, memory_order_relaxed);
    b->epoll_fd = -1;
    b->wake_fd  = -1;

    b->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (b->epoll_fd < 0) goto fail;

    b->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (b->wake_fd < 0) goto fail;

    struct epoll_event ev = { .events = EPOLLIN, .data = { .fd = b->wake_fd } };
    if (epoll_ctl(b->epoll_fd, EPOLL_CTL_ADD, b->wake_fd, &ev) < 0) goto fail;

    r->backend = b;
    return b;

fail:
    if (b->wake_fd  >= 0) close(b->wake_fd);
    if (b->epoll_fd >= 0) close(b->epoll_fd);
    mel_array_free(&b->inbox_write);
    mel_array_free(&b->inbox_read);
    free(b);
    return NULL;
}

static void mel__backend_free(Mel_Reactor_Internal* r)
{
    if (!r->backend) return;
    Mel_Reactor_Backend* b = r->backend;
    if (b->wake_fd  >= 0) close(b->wake_fd);
    if (b->epoll_fd >= 0) close(b->epoll_fd);
    mel_array_free(&b->inbox_write);
    mel_array_free(&b->inbox_read);
    free(b);
    r->backend = NULL;
}

static void mel__drain_inbox(Mel_Reactor_Internal* r)
{
    Mel_Reactor_Backend* b = r->backend;

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

bool mel__reactor_backend_adopt_system(Mel_Reactor_Internal* r)
{
    Mel_Reactor_Backend* b = mel__backend_alloc(r);
    if (!b) return false;
    b->thread     = pthread_self();
    b->thread_set = true;
    return true;
}

void mel__reactor_backend_release_system(Mel_Reactor_Internal* r)
{
    mel__backend_free(r);
}

bool mel__reactor_backend_create_user(Mel_Reactor_Internal* r)
{
    (void)r;
    fprintf(stderr, "[reactor] user reactors are not yet implemented on linux\n");
    return false;
}

void mel__reactor_backend_destroy_user(Mel_Reactor_Internal* r)
{
    (void)r;
}

void mel__reactor_backend_run(Mel_Reactor_Internal* r)
{
    if (!r->backend) return;
    Mel_Reactor_Backend* b = r->backend;

    if (pthread_equal(b->thread, pthread_self()) == 0) {
        fprintf(stderr, "[reactor] run invoked from a thread that does not own this reactor\n");
        abort();
    }

    for (;;) {
        struct epoll_event events[16];
        int n = epoll_wait(b->epoll_fd, events, 16, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == b->wake_fd) {
                u64 v;
                ssize_t rd = read(b->wake_fd, &v, sizeof(v));
                (void)rd;
            }
        }

        if (atomic_load_explicit(&b->stop_requested, memory_order_acquire)) return;

        mel__drain_inbox(r);
        mel__reactor_dispatch_update(r);
    }
}

void mel__reactor_backend_stop(Mel_Reactor_Internal* r)
{
    if (!r->backend) return;
    Mel_Reactor_Backend* b = r->backend;
    atomic_store_explicit(&b->stop_requested, true, memory_order_release);
    u64 one = 1;
    ssize_t wr = write(b->wake_fd, &one, sizeof(one));
    (void)wr;
}

void mel__reactor_backend_post(Mel_Reactor_Internal* r, void* message)
{
    if (!r->backend) return;
    Mel_Reactor_Backend* b = r->backend;

    mel__inbox_lock(b);
    mel_array_push(&b->inbox_write, message);
    mel__inbox_unlock(b);

    u64 one = 1;
    ssize_t wr = write(b->wake_fd, &one, sizeof(one));
    (void)wr;
}

bool mel__reactor_backend_owns_caller(Mel_Reactor_Internal* r)
{
    if (!r->backend || !r->backend->thread_set) return false;
    return pthread_equal(r->backend->thread, pthread_self()) != 0;
}
