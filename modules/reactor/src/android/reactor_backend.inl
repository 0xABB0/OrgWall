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
    r->android_looping = false;
    return true;
}

static void reactor_backend_shutdown(Mel_Reactor* r)
{
    if (r->android_looping) {
        ALooper_removeFd((ALooper*)r->looper, r->timer_fd);
        r->android_looping = false;
    }
    if (r->timer_fd >= 0) close(r->timer_fd);
    if (r->looper) ALooper_release((ALooper*)r->looper);
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

    if (timeout >= 0) {
        reactor_android_arm(r, timeout);
    } else if (poll_count > 0) {
        reactor_android_arm(r, 16);
    } else {
        reactor_android_disarm(r);
    }
    return true;
}
