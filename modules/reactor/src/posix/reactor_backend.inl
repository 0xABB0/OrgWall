#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

static void reactor_posix_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static bool reactor_backend_init(Mel_Reactor* r)
{
    if (pipe(r->wake_pipe) != 0) return false;
    reactor_posix_nonblock(r->wake_pipe[0]);
    reactor_posix_nonblock(r->wake_pipe[1]);
    return true;
}

static void reactor_backend_shutdown(Mel_Reactor* r)
{
    close(r->wake_pipe[0]);
    close(r->wake_pipe[1]);
}

static void reactor_backend_wake(Mel_Reactor* r)
{
    unsigned char byte = 1;
    ssize_t       n    = write(r->wake_pipe[1], &byte, 1);
    (void)n;
}

static bool reactor_backend_wait(Mel_Reactor* r, Mel_Reactor_Poll** polls, usize poll_count, i32 timeout)
{
    struct pollfd fds[MEL_REACTOR_MAX_POLLS + 1];
    fds[0].fd      = r->wake_pipe[0];
    fds[0].events  = POLLIN;
    fds[0].revents = 0;

    nfds_t n = 1;
    for (usize i = 0; i < poll_count && n <= MEL_REACTOR_MAX_POLLS; i++) {
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

    int rc = poll(fds, n, timeout < 0 ? -1 : (int)timeout);
    if (rc < 0) return true;

    if (fds[0].revents & POLLIN) {
        unsigned char drain[64];
        while (read(r->wake_pipe[0], drain, sizeof drain) > 0) { }
    }

    nfds_t slot = 1;
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
