#include <port/port.h>

#include <vat/vat.h>
#include <allocator/heap.h>
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
    Mel_Vat*      vat;
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
    mel_vat_quit(c->vat);
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
        mel_vat_quit(c->vat);
    }
    return true;
}

static i64 hup_idle_deadline(Mel_Vat_Source* s)
{
    (void)s;
    return 0;
}

static bool hup_idle_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    hup_idle(mel_vat_source_state(s));
    return false;
}

static const Mel_Alloc* g_vat_alloc;
static Mel_Vat_Waiter*  g_vat_waiter;
static Mel_Vat_Driver*  g_vat_driver;
static Mel_Vat*         g_vat;

static const Mel_Vat_Source_Vtbl HUP_IDLE_VT = {
    .wakeables = NULL,
    .deadline = hup_idle_deadline,
    .drain = hup_idle_drain,
    .cancel = NULL,
};

static void hup_run(Hup_Ctx* c)
{
    g_vat_alloc = mel_alloc_heap();
    g_vat_waiter = mel_vat_waiter_io(g_vat_alloc);
    g_vat_driver = mel_vat_driver_fair(g_vat_alloc, 64);
    g_vat = mel_vat_open(g_vat_alloc, (Mel_Vat_Desc){ .waiter = g_vat_waiter, .driver = g_vat_driver });
    c->vat = g_vat;
    c->port = mel_port_create(.vat = g_vat);
    c->exec = mel_port_executor(c->port);
    Mel_Vat_Source* idle = mel_vat_source_open(g_vat, &HUP_IDLE_VT, c);
    mel_vat_run(g_vat);
    mel_vat_source_close(idle);
}

static void hup_loop_close(void)
{
    mel_vat_close(g_vat);
    g_vat_driver->vt->close(g_vat_driver);
    g_vat_waiter->vt->close(g_vat_waiter);
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

    hup_run(c);
    mel_port_destroy(c->port);
    hup_loop_close();

    if (c->rfd >= 0)
        close(c->rfd);
    close(c->wfd);

    int rc = c->done ? 0 : 1;
    printf("%s\n", c->done ? "POLLHUP-COMPLETED" : "POLLHUP-HANG-REPRODUCED");
    free(c);
    return rc;
}
