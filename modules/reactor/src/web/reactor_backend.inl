#include <emscripten.h>
#include <poll.h>

static void reactor_web_tick(void* arg)
{
    Mel_Reactor* r = arg;
    if (!mel_reactor_iterate(r, true)) {
        r->web_looping = false;
        emscripten_cancel_main_loop();
    }
}

static bool reactor_backend_init(Mel_Reactor* r)
{
    r->web_looping = false;
    return true;
}

static void reactor_backend_shutdown(Mel_Reactor* r)
{
    if (r->web_looping) {
        r->web_looping = false;
        emscripten_cancel_main_loop();
    }
}

static void reactor_backend_wake(Mel_Reactor* r)
{
    if (!r->web_looping) {
        r->web_looping = true;
        emscripten_set_main_loop_arg(reactor_web_tick, r, 0, 0);
    }
}

static void reactor_backend_run(Mel_Reactor* r)
{
    r->web_looping = true;
    emscripten_set_main_loop_arg(reactor_web_tick, r, 0, 1);
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

    if (timeout < 0 && poll_count == 0) {
        r->web_looping = false;
        emscripten_cancel_main_loop();
    }
    return true;
}
