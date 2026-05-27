#ifdef _CLANGD
#pragma once
#include "../reactor.c"
#endif

#include <android/looper.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <unistd.h>

#define MEL_REACTOR_BACKEND_HAS_ATTACHED 1

static int reactor_android_tick(int fd, int events, void* data);
static int reactor_android_fd_cb(int fd, int events, void* data);

// Reconcile the looper's registered source fds to exactly the desired poll set:
// add the newly desired, remove those gone, leave stable ones in place. Each fd
// going ready wakes the looper, which arms the tick so the next iterate pulls
// its readiness with the non-blocking poll() in reactor_backend_wait. The
// add/remove syscalls happen only on change.
static void reactor_android_reconcile_fds(Mel_Reactor* r, Mel_Reactor_Poll** polls, usize poll_count)
{
    for (usize i = 0; i < r->reg_fd_count;) {
        bool desired = false;
        for (usize j = 0; j < poll_count; j++) {
            if (polls[j] && (int)polls[j]->handle == r->reg_fds[i]) { desired = true; break; }
        }
        if (desired) { i++; continue; }
        ALooper_removeFd((ALooper*)r->looper, r->reg_fds[i]);
        r->reg_fds[i] = r->reg_fds[--r->reg_fd_count];
    }

    for (usize j = 0; j < poll_count && r->reg_fd_count < MEL_REACTOR_MAX_POLLS; j++) {
        if (!polls[j]) continue;
        int fd = (int)polls[j]->handle;
        bool registered = false;
        for (usize i = 0; i < r->reg_fd_count; i++) {
            if (r->reg_fds[i] == fd) { registered = true; break; }
        }
        if (registered) continue;
        int events = 0;
        if (polls[j]->events & MEL_REACTOR_POLL_IN)  events |= ALOOPER_EVENT_INPUT;
        if (polls[j]->events & MEL_REACTOR_POLL_OUT) events |= ALOOPER_EVENT_OUTPUT;
        ALooper_addFd((ALooper*)r->looper, fd, 0, events, reactor_android_fd_cb, r);
        r->reg_fds[r->reg_fd_count++] = fd;
    }
}

static void reactor_android_arm(Mel_Reactor* r, i32 timeout_ms)
{
    struct itimerspec spec = {0};
    if (timeout_ms <= 0) {
        spec.it_value.tv_nsec = 1;
    } else {
        spec.it_value.tv_sec  = timeout_ms / 1000;
        spec.it_value.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
    }
    timerfd_settime(r->timer_fd, 0, &spec, NULL);
}

static void reactor_android_disarm(Mel_Reactor* r)
{
    struct itimerspec spec = {0};
    timerfd_settime(r->timer_fd, 0, &spec, NULL);
}

static bool reactor_backend_init(Mel_Reactor* r)
{
    ALooper* looper = ALooper_forThread();
    if (!looper) looper = ALooper_prepare(0);
    if (!looper) return false;
    ALooper_acquire(looper);

    r->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (r->timer_fd < 0) {
        ALooper_release(looper);
        return false;
    }
    r->looper          = looper;
    r->reg_fd_count    = 0;
    r->android_looping = false;
    return true;
}

static void reactor_backend_shutdown(Mel_Reactor* r)
{
    reactor_android_reconcile_fds(r, NULL, 0);
    if (r->android_looping) {
        ALooper_removeFd((ALooper*)r->looper, r->timer_fd);
        r->android_looping = false;
    }
    if (r->timer_fd >= 0) close(r->timer_fd);
    if (r->looper) ALooper_release((ALooper*)r->looper);
}

// A source fd going ready wakes the looper here; record nothing (the iterate's
// poll() pulls readiness) and just arm the tick so an iterate runs now.
static int reactor_android_fd_cb(int fd, int events, void* data)
{
    (void)fd; (void)events;
    reactor_android_arm((Mel_Reactor*)data, 0);
    return 1;
}

static int reactor_android_tick(int fd, int events, void* data)
{
    Mel_Reactor* r = data;
    u64          expirations;
    ssize_t      got = read(fd, &expirations, sizeof expirations);
    (void)events;
    (void)got;
    if (!r->init_done) {
        r->init_done = true;
        if (r->init && !r->init(r, r->init_user)) {
            r->android_looping = false;
            reactor_attached_destroy(r);
            return 0;
        }
    }
    if (!reactor_iterate(r, true)) {
        r->android_looping = false;
        reactor_attached_destroy(r);
        return 0;
    }
    return 1;
}

static void reactor_backend_attached_run(Mel_Reactor* r)
{
    if (!r->android_looping) {
        r->android_looping = true;
        ALooper_addFd((ALooper*)r->looper, r->timer_fd, 0,
                      ALOOPER_EVENT_INPUT, reactor_android_tick, r);
    }
    reactor_android_arm(r, 0);
}

static void reactor_backend_wake(Mel_Reactor* r)
{
    if (r->android_looping) {
        reactor_android_arm(r, 0);
        ALooper_wake((ALooper*)r->looper);
    }
}

static bool reactor_backend_wait(Mel_Reactor* r, Mel_Reactor_Poll** polls, usize poll_count, i32 timeout)
{
    reactor_android_reconcile_fds(r, polls, poll_count);

    struct pollfd fds[MEL_REACTOR_MAX_POLLS];
    nfds_t        n = 0;
    for (usize i = 0; i < poll_count && n < MEL_REACTOR_MAX_POLLS; i++) {
        if (!polls[i]) continue;
        polls[i]->revents = 0;
        short events = 0;
        if (polls[i]->events & MEL_REACTOR_POLL_IN)  events |= POLLIN;
        if (polls[i]->events & MEL_REACTOR_POLL_OUT) events |= POLLOUT;
        fds[n].fd      = (int)polls[i]->handle;
        fds[n].events  = events;
        fds[n].revents = 0;
        n++;
    }

    if (n > 0 && poll(fds, n, 0) > 0) {
        nfds_t slot = 0;
        for (usize i = 0; i < poll_count; i++) {
            if (!polls[i]) continue;
            if (slot >= n) break;
            short re  = fds[slot].revents;
            u32   out = 0;
            if (re & POLLIN)  out |= MEL_REACTOR_POLL_IN;
            if (re & POLLOUT) out |= MEL_REACTOR_POLL_OUT;
            if (re & POLLERR) out |= MEL_REACTOR_POLL_ERR;
            if (re & POLLHUP) out |= MEL_REACTOR_POLL_HUP;
            polls[i]->revents = out;
            slot++;
        }
    }

    // Timers set a deadline; fd readiness wakes the looper through the
    // registered fd callback. With neither pending the reactor is idle and
    // nothing is scheduled.
    if (timeout >= 0) reactor_android_arm(r, timeout);
    else              reactor_android_disarm(r);
    return true;
}
