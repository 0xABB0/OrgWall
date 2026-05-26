#ifdef _CLANGD
#pragma once
#include "../reactor.c"
#endif

#include <poll.h>

// wasm32-wasip1 is single-threaded and has no pipe()/eventfd, so there is no
// cross-thread wake to implement — wake is a no-op. Waiting uses poll(), which
// wasi-libc backs with poll_oneoff (clock + fd subscriptions); with no fds it
// degrades to a plain timeout sleep. The reactor runs in blocking (THREADED)
// mode, exactly like a native CLI; the attached/main-loop mode is web-only.

static bool reactor_backend_init(Mel_Reactor* r) { (void)r; return true; }
static void reactor_backend_shutdown(Mel_Reactor* r) { (void)r; }
static void reactor_backend_wake(Mel_Reactor* r) { (void)r; }

static bool reactor_backend_wait(Mel_Reactor* r, Mel_Reactor_Poll** polls, usize poll_count, i32 timeout)
{
    (void)r;
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

    // Nothing to wait on and no deadline: returning avoids an unbreakable block
    // (single-threaded, so nobody could wake us anyway).
    if (n == 0 && timeout < 0) return true;

    int rc = poll(fds, n, timeout < 0 ? -1 : (int)timeout);
    if (rc <= 0) return true;

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
    return true;
}
