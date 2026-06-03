#include <port/port.h>

#include <reactor/reactor.h>
#include <future/future.h>
#include <executor/executor.h>

#include <thread/thread.h>
#include <time/nano.h>
#include <collection.list/list.h>

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define N_OPS 64

typedef struct
{
    Mel_Task    task;
    Mel_Future* future;
    int         index;
    void*       owner;
} Race_Cont;

typedef struct
{
    Mel_Reactor*  reactor;
    Mel_Port*     port;
    Mel_Executor* exec;

    int turn;

    int rfds[N_OPS];
    int wfds[N_OPS];
    u8  bufs[N_OPS][16];

    Mel_Port_Op ops[N_OPS];
    Race_Cont   conts[N_OPS];

    atomic_int settle_count[N_OPS];
    atomic_int complete_seen[N_OPS];
    atomic_int cancel_seen[N_OPS];
    atomic_int cont_fires[N_OPS];

    atomic_int go;
    atomic_int armed;
    atomic_int finished_cancellers;
    Mel_Thread cancellers[N_OPS];

    atomic_int total_done;
} Race_Ctx;

static void race_cont(Mel_Task* self)
{
    Race_Cont* k = mel_container_of(self, Race_Cont, task);
    Race_Ctx*  c = (Race_Ctx*)k->owner;
    int        i = k->index;
    atomic_fetch_add(&c->cont_fires[i], 1);

    Mel_Future_Status fs = mel_future_status(k->future);
    if (mel_future_status_cancelled(fs))
        atomic_fetch_add(&c->cancel_seen[i], 1);
    else
        atomic_fetch_add(&c->complete_seen[i], 1);

    mel_port_future_release(k->future);
    atomic_fetch_add(&c->total_done, 1);
}

static int canceller_main(void* user)
{
    Race_Cont* k = (Race_Cont*)user;
    Race_Ctx*  c = (Race_Ctx*)k->owner;
    int        i = k->index;

    while (atomic_load(&c->go) == 0)
        mel_thread_yield();

    bool ok = mel_port_cancel(c->port, c->ops[i]);
    if (ok)
        atomic_fetch_add(&c->settle_count[i], 1);
    atomic_fetch_add(&c->finished_cancellers, 1);
    return 0;
}

static bool race_idle(void* user)
{
    Race_Ctx* c = (Race_Ctx*)user;
    c->turn++;

    if (c->turn == 2)
    {
        for (int i = 0; i < N_OPS; i++)
        {
            Mel_Future* f = mel_port_read(c->port, .fd = c->rfds[i], .buffer = c->bufs[i], .len = 4, .out_op = &c->ops[i]);
            c->conts[i].owner = c;
            c->conts[i].index = i;
            c->conts[i].future = f;
            mel_task_init(&c->conts[i].task, race_cont);
            mel_future_then(f, &c->conts[i].task, c->exec);
        }
        for (int i = 0; i < N_OPS; i++)
        {
            c->conts[i].index = i;
            c->conts[i].owner = c;
            mel_thread_spawn(&c->cancellers[i], canceller_main, &c->conts[i]);
        }
        atomic_store(&c->armed, 1);
    }

    if (c->turn == 3)
    {
        atomic_store(&c->go, 1);
        for (int i = 0; i < N_OPS; i++)
        {
            ssize_t w = write(c->wfds[i], "ping", 4);
            (void)w;
        }
    }

    if (atomic_load(&c->finished_cancellers) == N_OPS && atomic_load(&c->total_done) == N_OPS)
        mel_reactor_quit(c->reactor);

    if (c->turn > 200000)
        mel_reactor_quit(c->reactor);

    return true;
}

static bool race_init(Mel_Reactor* r, void* user)
{
    Race_Ctx* c = (Race_Ctx*)user;
    c->reactor = r;
    c->port = mel_port_create(.reactor = r);
    c->exec = mel_port_executor(c->port);
    Mel_Reactor_Source* idle = mel_reactor_idle_new(race_idle, c);
    mel_reactor_source_attach(r, idle);
    return true;
}

int main(void)
{
    Race_Ctx* c = calloc(1, sizeof *c);

    for (int i = 0; i < N_OPS; i++)
    {
        int fds[2];
        if (pipe(fds) != 0)
        {
            printf("FAIL pipe\n");
            return 2;
        }
        c->rfds[i] = fds[0];
        c->wfds[i] = fds[1];
    }

    mel_reactor_spawn(MEL_REACTOR_THREADED, race_init, c);

    for (int i = 0; i < N_OPS; i++)
        mel_thread_join(&c->cancellers[i], NULL);

    mel_port_destroy(c->port);

    int settled_twice = 0;
    int never_done = 0;
    int double_fire = 0;
    int both_seen = 0;
    for (int i = 0; i < N_OPS; i++)
    {
        int comp = atomic_load(&c->complete_seen[i]);
        int canc = atomic_load(&c->cancel_seen[i]);
        int fires = atomic_load(&c->cont_fires[i]);
        if (fires != 1)
            double_fire++;
        if (comp + canc != 1)
            both_seen++;
        if (comp == 0 && canc == 0)
            never_done++;
    }
    (void)settled_twice;

    for (int i = 0; i < N_OPS; i++)
    {
        close(c->rfds[i]);
        close(c->wfds[i]);
    }

    printf("ops=%d double_fire=%d both_or_none=%d never_done=%d total_done=%d\n", N_OPS, double_fire, both_seen, never_done, atomic_load(&c->total_done));

    int rc = (double_fire == 0 && both_seen == 0 && never_done == 0) ? 0 : 1;
    free(c);
    printf("%s\n", rc == 0 ? "RACE-HARNESS-PASS" : "RACE-HARNESS-FAIL");
    return rc;
}
