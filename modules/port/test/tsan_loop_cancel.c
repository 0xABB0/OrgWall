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

#define N_OPS 64

typedef struct
{
    Mel_Task    task;
    Mel_Future* future;
    int         index;
    void*       owner;
} Lc_Cont;

typedef struct
{
    Mel_Vat*      vat;
    Mel_Port*     port;
    Mel_Executor* exec;
    int           turn;

    int         rfds[N_OPS];
    int         wfds[N_OPS];
    u8          bufs[N_OPS][16];
    Mel_Port_Op ops[N_OPS];
    Lc_Cont     conts[N_OPS];

    int cancel_seen;
    int complete_seen;
    int fires;
    int done;
} Lc_Ctx;

static void lc_cont(Mel_Task* self)
{
    Lc_Cont* k = mel_container_of(self, Lc_Cont, task);
    Lc_Ctx*  c = (Lc_Ctx*)k->owner;
    c->fires++;
    if (mel_future_status_cancelled(mel_future_status(k->future)))
        c->cancel_seen++;
    else
        c->complete_seen++;
    mel_port_future_release(k->future);
    c->done++;
}

static bool lc_idle(void* user)
{
    Lc_Ctx* c = (Lc_Ctx*)user;
    c->turn++;

    if (c->turn == 2)
    {
        for (int i = 0; i < N_OPS; i++)
        {
            Mel_Future* f = mel_port_read(c->port, .fd = c->rfds[i], .buffer = c->bufs[i], .len = 4, .out_op = &c->ops[i]);
            c->conts[i].owner = c;
            c->conts[i].index = i;
            c->conts[i].future = f;
            mel_task_init(&c->conts[i].task, lc_cont);
            mel_future_then(f, &c->conts[i].task, c->exec);
        }
    }
    if (c->turn == 3)
    {
        for (int i = 0; i < N_OPS; i++)
        {
            if (i % 2 == 0)
            {
                ssize_t w = write(c->wfds[i], "ping", 4);
                (void)w;
            }
            else
            {
                mel_port_cancel(c->port, c->ops[i]);
            }
        }
    }
    if (c->done == N_OPS)
        mel_vat_quit(c->vat);
    if (c->turn > 200000)
        mel_vat_quit(c->vat);
    return true;
}

static i64 lc_idle_deadline(Mel_Vat_Source* s)
{
    (void)s;
    return 0;
}

static bool lc_idle_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    lc_idle(mel_vat_source_state(s));
    return false;
}

static const Mel_Alloc* g_vat_alloc;
static Mel_Vat_Waiter*  g_vat_waiter;
static Mel_Vat_Driver*  g_vat_driver;
static Mel_Vat*         g_vat;

static const Mel_Vat_Source_Vtbl LC_IDLE_VT = {
    .wakeables = NULL,
    .deadline = lc_idle_deadline,
    .drain = lc_idle_drain,
    .cancel = NULL,
};

static void lc_run(Lc_Ctx* c)
{
    g_vat_alloc = mel_alloc_heap();
    g_vat_waiter = mel_vat_waiter_io(g_vat_alloc);
    g_vat_driver = mel_vat_driver_fair(g_vat_alloc, 64);
    g_vat = mel_vat_open(g_vat_alloc, (Mel_Vat_Desc){ .waiter = g_vat_waiter, .driver = g_vat_driver });
    c->vat = g_vat;
    c->port = mel_port_create(.vat = g_vat);
    c->exec = mel_port_executor(c->port);
    Mel_Vat_Source* idle = mel_vat_source_open(g_vat, &LC_IDLE_VT, c);
    mel_vat_run(g_vat);
    mel_vat_source_close(idle);
}

static void lc_loop_close(void)
{
    mel_vat_close(g_vat);
    g_vat_driver->vt->close(g_vat_driver);
    g_vat_waiter->vt->close(g_vat_waiter);
}

int main(void)
{
    Lc_Ctx* c = calloc(1, sizeof *c);
    for (int i = 0; i < N_OPS; i++)
    {
        int fds[2];
        if (pipe(fds) != 0)
            return 2;
        c->rfds[i] = fds[0];
        c->wfds[i] = fds[1];
    }
    lc_run(c);
    mel_port_destroy(c->port);
    lc_loop_close();
    for (int i = 0; i < N_OPS; i++)
    {
        close(c->rfds[i]);
        close(c->wfds[i]);
    }
    int rc = (c->fires == N_OPS && c->cancel_seen + c->complete_seen == N_OPS) ? 0 : 1;
    printf("fires=%d complete=%d cancel=%d %s\n", c->fires, c->complete_seen, c->cancel_seen, rc == 0 ? "LOOP-CANCEL-PASS" : "LOOP-CANCEL-FAIL");
    free(c);
    return rc;
}
