#include <channel/channel.h>
#include <job/job.h>
#include <signal/signal.h>
#include <test/test.h>

#include <allocator/heap.h>
#include <allocator/allocator.h>
#include <collection/list.h>
#include <thread/thread.h>

#include <stdatomic.h>

#define CHANF_SPIN_LIMIT 200000000

static void chanf_spin_until(const _Atomic(i32)* flag, i32 want)
{
    for (i64 i = 0; i < CHANF_SPIN_LIMIT; i++)
    {
        if (atomic_load_explicit(flag, memory_order_acquire) == want)
            return;
        mel_thread_yield();
    }
}

typedef struct
{
    Mel_Channel*       ch;
    int                value;
    Mel_Channel_Status status;
    _Atomic(i32)       done;
} Send_Job;

static void send_job(void* data)
{
    Send_Job* j = (Send_Job*)data;
    j->status = mel_channel_send(j->ch, &j->value);
    atomic_fetch_add_explicit(&j->done, 1, memory_order_release);
}

typedef struct
{
    Mel_Channel*       ch;
    int                out;
    Mel_Channel_Status status;
    _Atomic(i32)       done;
} Recv_Job;

static void recv_job(void* data)
{
    Recv_Job* j = (Recv_Job*)data;
    j->status = mel_channel_recv(j->ch, &j->out);
    atomic_fetch_add_explicit(&j->done, 1, memory_order_release);
}

MEL_TEST(channel_fiber, blocking_send_parks_then_recv_resumes)
{
    mel_job_init();

    Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    Send_Job sj = { .ch = ch, .value = 0xCAFE, .status = 0xFFFF, .done = 0 };
    Recv_Job rj = { .ch = ch, .out = 0, .status = 0xFFFF, .done = 0 };

    mel_job_run(&sj, send_job, nullptr);
    mel_thread_sleep(20 * 1000 * 1000);
    MEL_EXPECT_EQ(atomic_load(&sj.done), 0);

    mel_job_run(&rj, recv_job, nullptr);

    chanf_spin_until(&sj.done, 1);
    chanf_spin_until(&rj.done, 1);

    MEL_EXPECT_EQ(sj.status, MEL_CHANNEL_OK);
    MEL_EXPECT_EQ(rj.status, MEL_CHANNEL_OK);
    MEL_EXPECT_EQ(rj.out, 0xCAFE);

    mel_channel_destroy(ch);
    mel_job_shutdown();
}

MEL_TEST(channel_fiber, blocking_recv_parks_then_send_resumes)
{
    mel_job_init();

    Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    Recv_Job rj = { .ch = ch, .out = 0, .status = 0xFFFF, .done = 0 };
    Send_Job sj = { .ch = ch, .value = 0xBEEF, .status = 0xFFFF, .done = 0 };

    mel_job_run(&rj, recv_job, nullptr);
    mel_thread_sleep(20 * 1000 * 1000);
    MEL_EXPECT_EQ(atomic_load(&rj.done), 0);

    mel_job_run(&sj, send_job, nullptr);

    chanf_spin_until(&rj.done, 1);
    chanf_spin_until(&sj.done, 1);

    MEL_EXPECT_EQ(rj.status, MEL_CHANNEL_OK);
    MEL_EXPECT_EQ(rj.out, 0xBEEF);

    mel_channel_destroy(ch);
    mel_job_shutdown();
}

MEL_TEST(channel_fiber, blocking_recv_on_closed_empty_returns_closed)
{
    mel_job_init();

    Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    Recv_Job rj = { .ch = ch, .out = 0, .status = 0xFFFF, .done = 0 };
    mel_job_run(&rj, recv_job, nullptr);

    mel_thread_sleep(20 * 1000 * 1000);
    MEL_EXPECT_EQ(atomic_load(&rj.done), 0);
    mel_channel_close(ch);

    chanf_spin_until(&rj.done, 1);
    MEL_EXPECT(mel_channel_status_closed(rj.status));

    mel_channel_destroy(ch);
    mel_job_shutdown();
}

MEL_TEST(channel_fiber, blocking_send_parked_then_closed_errors)
{
    mel_job_init();

    Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    Send_Job sj = { .ch = ch, .value = 1, .status = 0xFFFF, .done = 0 };
    mel_job_run(&sj, send_job, nullptr);

    mel_thread_sleep(20 * 1000 * 1000);
    MEL_EXPECT_EQ(atomic_load(&sj.done), 0);
    mel_channel_close(ch);

    chanf_spin_until(&sj.done, 1);
    MEL_EXPECT(mel_channel_status_failed(sj.status));
    MEL_EXPECT(mel_channel_status_closed(sj.status));

    mel_channel_destroy(ch);
    mel_job_shutdown();
}

typedef struct
{
    Mel_Channel* a;
    Mel_Channel* b;
    int          out_a;
    int          out_b;
    int          won_index;
    _Atomic(i32) done;
} Select_Job;

static void select_job(void* data)
{
    Select_Job* j = (Select_Job*)data;

    Mel_Channel_Op ops[2];
    mel_channel_op_recv(&ops[0], j->a, &j->out_a);
    mel_channel_op_recv(&ops[1], j->b, &j->out_b);

    Mel_Channel_Sel sel;
    mel_channel_sel_init(&sel, ops, 2);

    Mel_Channel_Op* won = mel_channel_sel_wait(&sel);
    j->won_index = won == &ops[0] ? 0 : (won == &ops[1] ? 1 : -1);

    atomic_fetch_add_explicit(&j->done, 1, memory_order_release);
}

MEL_TEST(channel_fiber, select_wakes_on_first_ready_channel)
{
    mel_job_init();

    Mel_Channel* a = mel_channel_create(sizeof(int), 0, mel_alloc_heap());
    Mel_Channel* b = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    Select_Job sj = { .a = a, .b = b, .out_a = 0, .out_b = 0, .won_index = -2, .done = 0 };
    mel_job_run(&sj, select_job, nullptr);

    mel_thread_sleep(20 * 1000 * 1000);
    MEL_EXPECT_EQ(atomic_load(&sj.done), 0);

    int v = 0x1357;
    for (;;)
    {
        if (mel_channel_try_send(b, &v) == MEL_CHANNEL_OK)
            break;
        mel_thread_yield();
    }

    chanf_spin_until(&sj.done, 1);

    MEL_EXPECT_EQ(sj.won_index, 1);
    MEL_EXPECT_EQ(sj.out_b, 0x1357);

    mel_channel_destroy(a);
    mel_channel_destroy(b);
    mel_job_shutdown();
}

MEL_TEST(channel_fiber, select_retracts_loser_no_phantom_delivery)
{
    mel_job_init();

    Mel_Channel* a = mel_channel_create(sizeof(int), 0, mel_alloc_heap());
    Mel_Channel* b = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    Select_Job sj = { .a = a, .b = b, .out_a = 0, .out_b = 0, .won_index = -2, .done = 0 };
    mel_job_run(&sj, select_job, nullptr);

    mel_thread_sleep(20 * 1000 * 1000);

    int v = 11;
    for (;;)
    {
        if (mel_channel_try_send(a, &v) == MEL_CHANNEL_OK)
            break;
        mel_thread_yield();
    }

    chanf_spin_until(&sj.done, 1);
    MEL_EXPECT_EQ(sj.won_index, 0);

    int leftover = 22;
    MEL_EXPECT(mel_channel_status_would_block(mel_channel_try_send(b, &leftover)));

    mel_channel_destroy(a);
    mel_channel_destroy(b);
    mel_job_shutdown();
}

MEL_TEST(channel_fiber, select_all_closed_returns_null)
{
    mel_job_init();

    Mel_Channel* a = mel_channel_create(sizeof(int), 0, mel_alloc_heap());
    Mel_Channel* b = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    Select_Job sj = { .a = a, .b = b, .out_a = 0, .out_b = 0, .won_index = -2, .done = 0 };
    mel_job_run(&sj, select_job, nullptr);

    mel_thread_sleep(20 * 1000 * 1000);
    mel_channel_close(a);
    mel_channel_close(b);

    chanf_spin_until(&sj.done, 1);
    MEL_EXPECT_EQ(sj.won_index, -1);

    mel_channel_destroy(a);
    mel_channel_destroy(b);
    mel_job_shutdown();
}

static void select_job_reversed(void* data)
{
    Select_Job* j = (Select_Job*)data;

    Mel_Channel_Op ops[2];
    mel_channel_op_recv(&ops[0], j->b, &j->out_b);
    mel_channel_op_recv(&ops[1], j->a, &j->out_a);

    Mel_Channel_Sel sel;
    mel_channel_sel_init(&sel, ops, 2);

    Mel_Channel_Op* won = mel_channel_sel_wait(&sel);
    j->won_index = won == &ops[1] ? 0 : (won == &ops[0] ? 1 : -1);

    atomic_fetch_add_explicit(&j->done, 1, memory_order_release);
}

#define SEL_DEADLOCK_ROUNDS 400

MEL_TEST(channel_fiber, select_opposite_lock_order_no_deadlock)
{
    mel_job_init();

    for (int r = 0; r < SEL_DEADLOCK_ROUNDS; r++)
    {
        Mel_Channel* a = mel_channel_create(sizeof(int), 0, mel_alloc_heap());
        Mel_Channel* b = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

        Select_Job ascending = { .a = a, .b = b, .out_a = 0, .out_b = 0, .won_index = -2, .done = 0 };
        Select_Job descending = { .a = a, .b = b, .out_a = 0, .out_b = 0, .won_index = -2, .done = 0 };

        mel_job_run(&ascending, select_job, nullptr);
        mel_job_run(&descending, select_job_reversed, nullptr);

        int va = 1;
        int vb = 2;
        for (;;)
        {
            if (mel_channel_try_send(a, &va) == MEL_CHANNEL_OK)
                break;
            mel_thread_yield();
        }
        for (;;)
        {
            if (mel_channel_try_send(b, &vb) == MEL_CHANNEL_OK)
                break;
            mel_thread_yield();
        }

        chanf_spin_until(&ascending.done, 1);
        chanf_spin_until(&descending.done, 1);

        MEL_EXPECT(ascending.won_index == 0 || ascending.won_index == 1);
        MEL_EXPECT(descending.won_index == 0 || descending.won_index == 1);

        int d = 0;
        while (mel_channel_try_recv(a, &d) == MEL_CHANNEL_OK)
            ;
        while (mel_channel_try_recv(b, &d) == MEL_CHANNEL_OK)
            ;

        mel_channel_destroy(a);
        mel_channel_destroy(b);
    }

    mel_job_shutdown();
}

typedef struct
{
    Mel_Channel* drain;
    int          first;
    int          per;
    _Atomic(i32) done;
} Mpmc_Send_Job;

static void mpmc_send_job(void* data)
{
    Mpmc_Send_Job* j = (Mpmc_Send_Job*)data;
    for (int i = 0; i < j->per; i++)
    {
        int v = j->first + i;
        mel_channel_send(j->drain, &v);
    }
    atomic_fetch_add_explicit(&j->done, 1, memory_order_release);
}

typedef struct
{
    Mel_Channel*  drain;
    _Atomic(i64)* sum;
    _Atomic(i64)* count;
    i64           target;
    _Atomic(i32)  done;
} Mpmc_Recv_Job;

static void mpmc_recv_job(void* data)
{
    Mpmc_Recv_Job* j = (Mpmc_Recv_Job*)data;
    for (;;)
    {
        i64 c = atomic_load_explicit(j->count, memory_order_acquire);
        if (c >= j->target)
            break;
        int                v = 0;
        Mel_Channel_Status s = mel_channel_recv(j->drain, &v);
        if (mel_channel_status_closed(s))
            break;
        atomic_fetch_add_explicit(j->sum, v, memory_order_relaxed);
        atomic_fetch_add_explicit(j->count, 1, memory_order_relaxed);
    }
    atomic_fetch_add_explicit(&j->done, 1, memory_order_release);
}

#define CHANF_PRODUCERS 3
#define CHANF_CONSUMERS 3
#define CHANF_PER       2000

MEL_TEST(channel_fiber, mpmc_blocking_count_and_sum_invariant)
{
    mel_job_init();

    Mel_Channel* drain = mel_channel_create(sizeof(int), 16, mel_alloc_heap());

    _Atomic(i64) sum = 0;
    _Atomic(i64) count = 0;
    _Atomic(i32) prod_done = 0;
    _Atomic(i32) cons_done = 0;

    i64 expect_count = (i64)CHANF_PRODUCERS * CHANF_PER;
    i64 expect_sum = 0;

    Mpmc_Send_Job sjobs[CHANF_PRODUCERS];
    for (int t = 0; t < CHANF_PRODUCERS; t++)
    {
        sjobs[t] = (Mpmc_Send_Job){ .drain = drain, .first = t * CHANF_PER, .per = CHANF_PER, .done = 0 };
        for (int i = 0; i < CHANF_PER; i++)
            expect_sum += (i64)(t * CHANF_PER + i);
    }

    Mpmc_Recv_Job rjobs[CHANF_CONSUMERS];
    for (int t = 0; t < CHANF_CONSUMERS; t++)
        rjobs[t] = (Mpmc_Recv_Job){ .drain = drain, .sum = &sum, .count = &count, .target = expect_count, .done = 0 };

    for (int t = 0; t < CHANF_CONSUMERS; t++)
        mel_job_run(&rjobs[t], mpmc_recv_job, nullptr);
    for (int t = 0; t < CHANF_PRODUCERS; t++)
        mel_job_run(&sjobs[t], mpmc_send_job, nullptr);

    for (int t = 0; t < CHANF_PRODUCERS; t++)
        chanf_spin_until(&sjobs[t].done, 1);

    for (i64 i = 0; i < CHANF_SPIN_LIMIT; i++)
    {
        if (atomic_load_explicit(&count, memory_order_acquire) >= expect_count)
            break;
        mel_thread_yield();
    }

    mel_channel_close(drain);

    for (int t = 0; t < CHANF_CONSUMERS; t++)
        chanf_spin_until(&rjobs[t].done, 1);

    MEL_EXPECT_EQ((i64)atomic_load(&count), expect_count);
    MEL_EXPECT_EQ((i64)atomic_load(&sum), expect_sum);

    mel_channel_destroy(drain);
    mel_job_shutdown();
}

typedef struct
{
    Mel_Channel*  ch;
    _Atomic(i32)* closed_count;
    _Atomic(i32)* ok_count;
    _Atomic(i32)  done;
} Race_Recv_Job;

static void race_recv_job(void* data)
{
    Race_Recv_Job*     j = (Race_Recv_Job*)data;
    int                v = 0;
    Mel_Channel_Status s = mel_channel_recv(j->ch, &v);
    if (mel_channel_status_closed(s))
        atomic_fetch_add_explicit(j->closed_count, 1, memory_order_relaxed);
    else
        atomic_fetch_add_explicit(j->ok_count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&j->done, 1, memory_order_release);
}

#define RACE_ROUNDS 400

MEL_TEST(channel_fiber, close_vs_parked_recv_single_outcome)
{
    mel_job_init();

    _Atomic(i32) closed_count = 0;
    _Atomic(i32) ok_count = 0;

    for (int r = 0; r < RACE_ROUNDS; r++)
    {
        Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

        Race_Recv_Job rj = { .ch = ch, .closed_count = &closed_count, .ok_count = &ok_count, .done = 0 };
        mel_job_run(&rj, race_recv_job, nullptr);

        mel_thread_sleep(1000 * 1000);
        mel_channel_close(ch);

        chanf_spin_until(&rj.done, 1);
        mel_channel_destroy(ch);
    }

    MEL_EXPECT_EQ((i32)atomic_load(&closed_count), RACE_ROUNDS);
    MEL_EXPECT_EQ((i32)atomic_load(&ok_count), 0);

    mel_job_shutdown();
}

typedef struct
{
    Mel_Channel* a;
    Mel_Channel* b;
    int          out_a;
    int          out_b;
    int          won_index;
    _Atomic(i32) done;
} Sel_Retract_Job;

static void sel_retract_job(void* data)
{
    Sel_Retract_Job* j = (Sel_Retract_Job*)data;

    Mel_Channel_Op ops[2];
    mel_channel_op_recv(&ops[0], j->a, &j->out_a);
    mel_channel_op_recv(&ops[1], j->b, &j->out_b);

    Mel_Channel_Sel sel;
    mel_channel_sel_init(&sel, ops, 2);

    Mel_Channel_Op* won = mel_channel_sel_wait(&sel);
    j->won_index = won == &ops[0] ? 0 : (won == &ops[1] ? 1 : -1);

    atomic_fetch_add_explicit(&j->done, 1, memory_order_release);
}

typedef struct
{
    Mel_Channel*  ch;
    int           value;
    _Atomic(i32)* go;
    _Atomic(i32)  taken;
    _Atomic(i32)  done;
} Sel_Loser_Sender;

static int sel_loser_sender_main(void* user)
{
    Sel_Loser_Sender* s = (Sel_Loser_Sender*)user;
    chanf_spin_until(s->go, 1);
    Mel_Channel_Status st = mel_channel_try_send(s->ch, &s->value);
    atomic_store_explicit(&s->taken, st == MEL_CHANNEL_OK ? 1 : 0, memory_order_relaxed);
    atomic_store_explicit(&s->done, 1, memory_order_release);
    return 0;
}

#define SEL_RETRACT_ROUNDS 600

MEL_TEST(channel_fiber, select_loser_retract_vs_concurrent_arrival)
{
    mel_job_init();

    i64 won_a = 0;
    i64 won_b = 0;

    for (int r = 0; r < SEL_RETRACT_ROUNDS; r++)
    {
        Mel_Channel* a = mel_channel_create(sizeof(int), 0, mel_alloc_heap());
        Mel_Channel* b = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

        Sel_Retract_Job sj = { .a = a, .b = b, .out_a = 0, .out_b = 0, .won_index = -2, .done = 0 };
        mel_job_run(&sj, sel_retract_job, nullptr);

        _Atomic(i32)     go = 0;
        Sel_Loser_Sender ls = { .ch = b, .value = 200, .go = &go, .taken = 0, .done = 0 };
        Mel_Thread       th;
        MEL_REQUIRE(mel_thread_spawn(&th, sel_loser_sender_main, &ls, .name = "sel-loser"));

        int va = 100;
        for (;;)
        {
            if (mel_channel_try_send(a, &va) == MEL_CHANNEL_OK)
                break;
            mel_thread_yield();
        }
        atomic_store_explicit(&go, 1, memory_order_release);

        chanf_spin_until(&sj.done, 1);
        chanf_spin_until(&ls.done, 1);

        int rc = 0;
        mel_thread_join(&th, &rc);

        bool b_taken = atomic_load_explicit(&ls.taken, memory_order_relaxed) == 1;

        if (sj.won_index == 0)
        {
            won_a++;
            MEL_EXPECT_EQ(sj.out_a, 100);
            MEL_EXPECT(!b_taken);
        }
        else
        {
            won_b++;
            MEL_EXPECT(b_taken);
            MEL_EXPECT_EQ(sj.out_b, 200);
        }

        int d = 0;
        while (mel_channel_try_recv(a, &d) == MEL_CHANNEL_OK)
            ;
        while (mel_channel_try_recv(b, &d) == MEL_CHANNEL_OK)
            ;

        mel_channel_destroy(a);
        mel_channel_destroy(b);
    }

    MEL_EXPECT_EQ(won_a + won_b, (i64)SEL_RETRACT_ROUNDS);

    mel_job_shutdown();
}
