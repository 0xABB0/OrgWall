#ifdef _CLANGD
#pragma once
#include "../reactor.c"
#endif

#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/html5.h>
#include <poll.h>

// The browser owns the loop; the reactor hands it one iteration at a time and
// honors the folded timeout the core computed from its sources:
//   0       -> requestAnimationFrame  (idle/fast source: the vsync-paced game loop)
//   finite  -> setTimeout             (a timer source's deadline)
//   FOREVER -> nothing                (event-driven app, idle; wake() re-arms)
// No persistent frame loop, no per-frame polling for timers.
#define MEL_REACTOR_BACKEND_HAS_ATTACHED 1

enum { MEL_WEB_IDLE = 0, MEL_WEB_RAF, MEL_WEB_TIMEOUT };

static void reactor_web_drive(Mel_Reactor* r)
{
    if (!reactor_iterate(r, true)) reactor_attached_destroy(r);
}

static void reactor_web_cancel(Mel_Reactor* r)
{
    if (r->web_kind == MEL_WEB_RAF)          emscripten_cancel_animation_frame(r->web_id);
    else if (r->web_kind == MEL_WEB_TIMEOUT) emscripten_clear_timeout(r->web_id);
    r->web_kind = MEL_WEB_IDLE;
}

static EM_BOOL reactor_web_raf_cb(double time, void* arg)
{
    (void)time;
    Mel_Reactor* r = arg;
    r->web_kind = MEL_WEB_IDLE;
    reactor_web_drive(r);
    return EM_FALSE;  // one-shot; the iterate re-arms if it still needs a frame
}

static void reactor_web_timeout_cb(void* arg)
{
    Mel_Reactor* r = arg;
    r->web_kind = MEL_WEB_IDLE;
    reactor_web_drive(r);
}

static void reactor_web_schedule(Mel_Reactor* r, i32 timeout, usize poll_count)
{
    reactor_web_cancel(r);
    // FOREVER + fds is the one path the browser can't wait on natively (no
    // fd-readiness wakeup; async sockets use emscripten's socket callbacks
    // instead), so re-poll on the frame clock. Unreached by the current web
    // build, which has no fd sources.
    if (timeout < 0 && poll_count > 0) timeout = 0;

    if (timeout == 0) {
        r->web_id   = emscripten_request_animation_frame(reactor_web_raf_cb, r);
        r->web_kind = MEL_WEB_RAF;
    } else if (timeout > 0) {
        r->web_id   = emscripten_set_timeout(reactor_web_timeout_cb, (double)timeout, r);
        r->web_kind = MEL_WEB_TIMEOUT;
    }
}

static bool reactor_backend_init(Mel_Reactor* r)
{
    r->web_kind = MEL_WEB_IDLE;
    return true;
}

static void reactor_backend_shutdown(Mel_Reactor* r)
{
    reactor_web_cancel(r);
}

static void reactor_backend_wake(Mel_Reactor* r)
{
    if (r->web_kind != MEL_WEB_IDLE) return;
    r->web_id   = emscripten_set_timeout(reactor_web_timeout_cb, 0, r);
    r->web_kind = MEL_WEB_TIMEOUT;
}

static void reactor_backend_attached_run(Mel_Reactor* r)
{
    // Run init on the calling (main) thread, let the first iterate schedule the
    // next wakeup only if a source needs one, then hand control back to the
    // browser while keeping the module alive. Later iterations come from the
    // scheduled rAF/timeout and from DOM event handlers that call wake().
    if (r->init && !r->init(r, r->init_user)) {
        atomic_store(&r->running, false);
        reactor_attached_destroy(r);
        return;
    }
    r->init_done = true;
    if (!reactor_iterate(r, true)) { reactor_attached_destroy(r); return; }
    emscripten_exit_with_live_runtime();
}

static bool reactor_backend_wait(Mel_Reactor* r, Mel_Reactor_Poll** polls, usize poll_count, i32 timeout)
{
    // Capture current fd readiness for this iterate. The web build has no socket
    // or file sources, so poll_count is normally zero.
    struct pollfd fds[MEL_REACTOR_MAX_POLLS];
    nfds_t        n = 0;
    for (usize i = 0; i < poll_count && n < MEL_REACTOR_MAX_POLLS; i++) {
        if (!polls[i]) continue;
        polls[i]->revents = 0;
        short events = 0;
        if (polls[i]->events & MEL_REACTOR_POLL_IN)  events |= POLLIN;
        if (polls[i]->events & MEL_REACTOR_POLL_OUT) events |= POLLOUT;
        fds[n].fd = (int)polls[i]->handle; fds[n].events = events; fds[n].revents = 0;
        n++;
    }
    if (n > 0 && poll(fds, n, 0) > 0) {
        nfds_t slot = 0;
        for (usize i = 0; i < poll_count; i++) {
            if (!polls[i]) continue;
            if (slot >= n) break;
            short re = fds[slot].revents; u32 out = 0;
            if (re & POLLIN)  out |= MEL_REACTOR_POLL_IN;
            if (re & POLLOUT) out |= MEL_REACTOR_POLL_OUT;
            if (re & POLLERR) out |= MEL_REACTOR_POLL_ERR;
            if (re & POLLHUP) out |= MEL_REACTOR_POLL_HUP;
            polls[i]->revents = out;
            slot++;
        }
    }

    reactor_web_schedule(r, timeout, poll_count);
    return true;
}
