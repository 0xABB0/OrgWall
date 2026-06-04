#include <port/port.h>

#include <reactor/reactor.h>
#include <future/future.h>
#include <executor/executor.h>

#include <thread/thread.h>
#include <time/nano.h>
#include <collection/list.h>

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <signal.h>

typedef struct
{
    Mel_Task    task;
    Mel_Future* future;
    void*       owner;
} Hup_Cont;

typedef struct
{
    Mel_Reactor*  reactor;
    Mel_Port*     port;
    Mel_Executor* exec;
    int           turn;

    int rfd;
    int wfd;

    u8          big[1 << 20];
    Mel_Port_Op op;
    Hup_Cont    cont;

    int  fires;
    bool done;
    bool peer_closed;
    int  close_turn;
    int  max_turns;
} Hup_Ctx;

static void hup_cont(Mel_Task* self)
{
    Hup_Cont* k = mel_container_of(self, Hup_Cont, task);
    Hup_Ctx*  c = (Hup_Ctx*)k->owner;
    c->fires++;
    c->done = true;
    const Mel_Port_Result* r = mel_port_future_result(k->future);
    printf("WRITE COMPLETED bytes=%zu status=0x%x os_error=%d\n", r->bytes_transferred, r->status, r->os_error);
    mel_port_future_release(k->future);
    mel_reactor_quit(c->reactor);
}

static bool hup_idle(void* user)
{
    Hup_Ctx* c = (Hup_Ctx*)user;
    c->turn++;

    if (c->turn == 2)
    {
        fcntl(c->wfd, F_SETFL, fcntl(c->wfd, F_GETFL, 0) | O_NONBLOCK);
        for (;;)
        {
            ssize_t n = write(c->wfd, c->big, sizeof c->big);
            if (n < 0)
                break;
        }
        shutdown(c->rfd, SHUT_RDWR);
        Mel_Future* f = mel_port_write(c->port, .fd = c->wfd, .buffer = c->big, .len = sizeof c->big, .out_op = &c->op);
        c->cont.owner = c;
        c->cont.future = f;
        mel_task_init(&c->cont.task, hup_cont);
        mel_future_then(f, &c->cont.task, c->exec);
        printf("submitted write on full buffer with peer half-closed; pending=%u\n", mel_port_pending(c->port));
    }

    if (!c->done && c->turn >= c->max_turns)
    {
        printf("TIMEOUT: write op never completed after %d turns; pending=%u (HANG)\n", c->turn, mel_port_pending(c->port));
        mel_reactor_quit(c->reactor);
    }
    return true;
}

static bool hup_init(Mel_Reactor* r, void* user)
{
    Hup_Ctx* c = (Hup_Ctx*)user;
    c->reactor = r;
    c->port = mel_port_create(.reactor = r);
    c->exec = mel_port_executor(c->port);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(hup_idle, c);
    mel_reactor_source_attach(r, idle);
    return true;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);
    fprintf(stderr, "start\n");
    Hup_Ctx* c = calloc(1, sizeof *c);
    int      sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return 2;

    int sz = 4096;
    setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);
    setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);

    c->rfd = sv[0];
    c->wfd = sv[1];
    c->close_turn = 200;
    c->max_turns = 500;

    memset(c->big, 'x', sizeof c->big);

    mel_reactor_spawn(MEL_REACTOR_THREADED, hup_init, c);
    mel_port_destroy(c->port);

    if (c->rfd >= 0)
        close(c->rfd);
    close(c->wfd);

    int rc = c->done ? 0 : 1;
    printf("%s\n", c->done ? "POLLHUP-COMPLETED" : "POLLHUP-HANG-REPRODUCED");
    free(c);
    return rc;
}
