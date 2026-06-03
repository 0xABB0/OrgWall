#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <signal.h>

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return 2;
    int sz = 4096;
    setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);
    setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);
    fcntl(sv[1], F_SETFL, fcntl(sv[1], F_GETFL, 0) | O_NONBLOCK);

    char buf[8192];
    memset(buf, 'x', sizeof buf);
    ssize_t total = 0;
    for (;;)
    {
        ssize_t n = write(sv[1], buf, sizeof buf);
        if (n < 0)
        {
            printf("write filled, errno=%d (EAGAIN=%d) total=%zd\n", errno, EAGAIN, total);
            break;
        }
        total += n;
    }

    shutdown(sv[0], SHUT_RDWR);
    printf("peer shutdown(SHUT_RDWR) issued, fd still open\n");

    int           kq = kqueue();
    struct kevent ev;
    EV_SET(&ev, sv[1], EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, NULL);
    kevent(kq, &ev, 1, NULL, 0, NULL);

    struct kevent   out;
    struct timespec ts = { 2, 0 };
    int             r = kevent(kq, NULL, 0, &out, 1, &ts);
    if (r == 0)
    {
        printf("EVFILT_WRITE: NO EVENT in 2s (write filter starved -> port would HANG)\n");
    }
    else
    {
        printf("EVFILT_WRITE fired: flags=0x%x (EV_EOF=0x%x) data=%ld fflags=0x%x\n", out.flags, EV_EOF, (long)out.data, out.fflags);
    }

    char    poke[1];
    ssize_t w = write(sv[1], poke, 1);
    printf("post-shutdown write() => n=%zd errno=%d (EPIPE=%d)\n", w, errno, EPIPE);

    close(sv[0]);
    close(sv[1]);
    close(kq);
    return 0;
}
