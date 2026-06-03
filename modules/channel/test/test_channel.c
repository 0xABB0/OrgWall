#include <channel/channel.h>
#include <test/test.h>

#include <allocator/heap.h>
#include <allocator/allocator.h>
#include <collection.list/list.h>
#include <executor/executor.h>
#include <future/future.h>
#include <thread/thread.h>
#include <thread/barrier.h>

#include <stdatomic.h>

#define CHAN_SPIN_LIMIT 200000000

MEL_TEST(channel, create_reports_geometry)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 4, mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(ch);
    MEL_EXPECT_EQ(mel_channel_item_size(ch), sizeof(int));
    MEL_EXPECT_EQ(mel_channel_capacity(ch), (usize)4);
    MEL_EXPECT(!mel_channel_is_closed(ch));
    mel_channel_destroy(ch);
}

MEL_TEST(channel, buffered_fill_drain_fifo)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 3, mel_alloc_heap());

    for (int i = 0; i < 3; i++)
        MEL_EXPECT_EQ(mel_channel_try_send(ch, &i), MEL_CHANNEL_OK);

    int overflow = 99;
    MEL_EXPECT(mel_channel_status_would_block(mel_channel_try_send(ch, &overflow)));

    for (int i = 0; i < 3; i++)
    {
        int out = -1;
        MEL_EXPECT_EQ(mel_channel_try_recv(ch, &out), MEL_CHANNEL_OK);
        MEL_EXPECT_EQ(out, i);
    }

    int drained = -1;
    MEL_EXPECT(mel_channel_status_would_block(mel_channel_try_recv(ch, &drained)));

    mel_channel_destroy(ch);
}

MEL_TEST(channel, buffered_wraparound_preserves_order)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 2, mel_alloc_heap());

    int a = 1, b = 2;
    MEL_EXPECT_EQ(mel_channel_try_send(ch, &a), MEL_CHANNEL_OK);
    MEL_EXPECT_EQ(mel_channel_try_send(ch, &b), MEL_CHANNEL_OK);

    int out = 0;
    MEL_EXPECT_EQ(mel_channel_try_recv(ch, &out), MEL_CHANNEL_OK);
    MEL_EXPECT_EQ(out, 1);

    int c = 3;
    MEL_EXPECT_EQ(mel_channel_try_send(ch, &c), MEL_CHANNEL_OK);

    MEL_EXPECT_EQ(mel_channel_try_recv(ch, &out), MEL_CHANNEL_OK);
    MEL_EXPECT_EQ(out, 2);
    MEL_EXPECT_EQ(mel_channel_try_recv(ch, &out), MEL_CHANNEL_OK);
    MEL_EXPECT_EQ(out, 3);

    mel_channel_destroy(ch);
}

MEL_TEST(channel, unbuffered_try_send_blocks_without_receiver)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());
    int          v = 7;
    MEL_EXPECT(mel_channel_status_would_block(mel_channel_try_send(ch, &v)));
    int out = 0;
    MEL_EXPECT(mel_channel_status_would_block(mel_channel_try_recv(ch, &out)));
    mel_channel_destroy(ch);
}

MEL_TEST(channel, unbuffered_rendezvous_hands_value_directly)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    int        out = 0;
    Mel_Future f;
    mel_channel_recv_future(ch, &out, &f, mel_executor_inline(), mel_alloc_heap());
    MEL_EXPECT(!mel_future_resolved(&f));

    int v = 0x55AA;
    MEL_EXPECT_EQ(mel_channel_try_send(ch, &v), MEL_CHANNEL_OK);

    MEL_REQUIRE(mel_future_resolved(&f));
    MEL_EXPECT_EQ(mel_future_status(&f) & MEL_FUTURE_SEVERITY_MASK, MEL_FUTURE_OK);
    MEL_EXPECT_EQ(out, 0x55AA);

    mel_channel_destroy(ch);
}

MEL_TEST(channel, unbuffered_rendezvous_send_parks_recv_completes)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    int        v = 0x1234;
    Mel_Future f;
    mel_channel_send_future(ch, &v, &f, mel_executor_inline(), mel_alloc_heap());
    MEL_EXPECT(!mel_future_resolved(&f));

    int out = 0;
    MEL_EXPECT_EQ(mel_channel_try_recv(ch, &out), MEL_CHANNEL_OK);
    MEL_EXPECT_EQ(out, 0x1234);

    MEL_REQUIRE(mel_future_resolved(&f));
    MEL_EXPECT_EQ(mel_future_status(&f) & MEL_FUTURE_SEVERITY_MASK, MEL_FUTURE_OK);

    mel_channel_destroy(ch);
}

MEL_TEST(channel, close_then_recv_drains_then_reports_closed)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 4, mel_alloc_heap());

    for (int i = 0; i < 3; i++)
        mel_channel_try_send(ch, &i);

    mel_channel_close(ch);
    MEL_EXPECT(mel_channel_is_closed(ch));

    for (int i = 0; i < 3; i++)
    {
        int out = -1;
        MEL_EXPECT_EQ(mel_channel_try_recv(ch, &out), MEL_CHANNEL_OK);
        MEL_EXPECT_EQ(out, i);
    }

    int out = -1;
    MEL_EXPECT(mel_channel_status_closed(mel_channel_try_recv(ch, &out)));

    mel_channel_destroy(ch);
}

MEL_TEST(channel, send_after_close_errors)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 4, mel_alloc_heap());
    mel_channel_close(ch);

    int                v = 1;
    Mel_Channel_Status s = mel_channel_try_send(ch, &v);
    MEL_EXPECT(mel_channel_status_failed(s));
    MEL_EXPECT(mel_channel_status_closed(s));

    mel_channel_destroy(ch);
}

MEL_TEST(channel, double_close_is_idempotent)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 1, mel_alloc_heap());
    mel_channel_close(ch);
    mel_channel_close(ch);
    MEL_EXPECT(mel_channel_is_closed(ch));
    mel_channel_destroy(ch);
}

typedef struct
{
    Mel_Task          task;
    int               ran;
    Mel_Future_Status status;
    Mel_Future*       fut;
} Fcont;

static void fcont_run(Mel_Task* self)
{
    Fcont* c = mel_container_of(self, Fcont, task);
    c->ran++;
    c->status = mel_future_status(c->fut);
}

MEL_TEST(channel, future_send_resolves_into_buffer)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 1, mel_alloc_heap());

    int        v = 42;
    Mel_Future f;
    mel_channel_send_future(ch, &v, &f, mel_executor_inline(), mel_alloc_heap());

    Fcont c = { 0 };
    c.fut = &f;
    mel_task_init(&c.task, fcont_run);
    mel_future_then(&f, &c.task, mel_executor_inline());

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT_EQ(c.status & MEL_CHANNEL_SEVERITY_MASK, MEL_CHANNEL_OK);

    int out = 0;
    MEL_EXPECT_EQ(mel_channel_try_recv(ch, &out), MEL_CHANNEL_OK);
    MEL_EXPECT_EQ(out, 42);

    mel_channel_destroy(ch);
}

MEL_TEST(channel, future_recv_pending_then_resolved_by_send)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    int        out = 0;
    Mel_Future f;
    mel_channel_recv_future(ch, &out, &f, mel_executor_inline(), mel_alloc_heap());

    Fcont c = { 0 };
    c.fut = &f;
    mel_task_init(&c.task, fcont_run);
    mel_future_then(&f, &c.task, mel_executor_inline());

    MEL_EXPECT_EQ(c.ran, 0);
    MEL_EXPECT(!mel_future_resolved(&f));

    int v = 1234;
    MEL_EXPECT_EQ(mel_channel_try_send(ch, &v), MEL_CHANNEL_OK);

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT_EQ(out, 1234);
    MEL_EXPECT_EQ(c.status & MEL_CHANNEL_SEVERITY_MASK, MEL_CHANNEL_OK);

    mel_channel_destroy(ch);
}

MEL_TEST(channel, future_recv_resolves_closed_when_closed_empty)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    int        out = 0;
    Mel_Future f;
    mel_channel_recv_future(ch, &out, &f, mel_executor_inline(), mel_alloc_heap());

    MEL_EXPECT(!mel_future_resolved(&f));

    mel_channel_close(ch);

    MEL_REQUIRE(mel_future_resolved(&f));
    Mel_Future_Status s = mel_future_status(&f);
    MEL_EXPECT(mel_future_status_failed(s));
    MEL_EXPECT((s & MEL_FUTURE_BROKEN) != 0u);

    mel_channel_destroy(ch);
}

MEL_TEST(channel, future_send_blocked_then_closed_errors)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    int        v = 5;
    Mel_Future f;
    mel_channel_send_future(ch, &v, &f, mel_executor_inline(), mel_alloc_heap());
    MEL_EXPECT(!mel_future_resolved(&f));

    mel_channel_close(ch);

    MEL_REQUIRE(mel_future_resolved(&f));
    Mel_Future_Status s = mel_future_status(&f);
    MEL_EXPECT(mel_future_status_failed(s));
    MEL_EXPECT((s & MEL_FUTURE_BROKEN) != 0u);

    mel_channel_destroy(ch);
}

MEL_TEST(channel, sel_try_picks_first_ready)
{
    Mel_Channel* a = mel_channel_create(sizeof(int), 1, mel_alloc_heap());
    Mel_Channel* b = mel_channel_create(sizeof(int), 1, mel_alloc_heap());

    int seed = 77;
    mel_channel_try_send(b, &seed);

    int out_a = 0, out_b = 0;

    Mel_Channel_Op ops[2];
    mel_channel_op_recv(&ops[0], a, &out_a);
    mel_channel_op_recv(&ops[1], b, &out_b);

    Mel_Channel_Sel sel;
    mel_channel_sel_init(&sel, ops, 2);

    Mel_Channel_Op* won = mel_channel_sel_try(&sel);
    MEL_REQUIRE_NOT_NULL(won);
    MEL_EXPECT_EQ(won, &ops[1]);
    MEL_EXPECT_EQ(out_b, 77);

    mel_channel_destroy(a);
    mel_channel_destroy(b);
}

MEL_TEST(channel, sel_try_none_ready_returns_null)
{
    Mel_Channel* a = mel_channel_create(sizeof(int), 1, mel_alloc_heap());
    Mel_Channel* b = mel_channel_create(sizeof(int), 1, mel_alloc_heap());

    int            out_a = 0, out_b = 0;
    Mel_Channel_Op ops[2];
    mel_channel_op_recv(&ops[0], a, &out_a);
    mel_channel_op_recv(&ops[1], b, &out_b);

    Mel_Channel_Sel sel;
    mel_channel_sel_init(&sel, ops, 2);

    MEL_EXPECT_NULL(mel_channel_sel_try(&sel));

    mel_channel_destroy(a);
    mel_channel_destroy(b);
}

typedef struct
{
    Mel_Channel* producers[4];
    Mel_Channel* drain;
    Mel_Barrier* start;
    int          first;
    int          per;
    _Atomic(i32) done;
} Mpmc_Producer;

static int mpmc_producer_main(void* user)
{
    Mpmc_Producer* p = (Mpmc_Producer*)user;
    mel_barrier_wait(p->start);
    for (int i = 0; i < p->per; i++)
    {
        int v = p->first + i;
        for (;;)
        {
            if (mel_channel_try_send(p->drain, &v) == MEL_CHANNEL_OK)
                break;
            mel_thread_yield();
        }
    }
    atomic_store_explicit(&p->done, 1, memory_order_release);
    return 0;
}

typedef struct
{
    Mel_Channel*  drain;
    Mel_Barrier*  start;
    _Atomic(i64)* sum;
    _Atomic(i64)* count;
    _Atomic(i32)* stop;
} Mpmc_Consumer;

static int mpmc_consumer_main(void* user)
{
    Mpmc_Consumer* c = (Mpmc_Consumer*)user;
    mel_barrier_wait(c->start);
    for (;;)
    {
        int                v = 0;
        Mel_Channel_Status s = mel_channel_try_recv(c->drain, &v);
        if (s == MEL_CHANNEL_OK)
        {
            atomic_fetch_add_explicit(c->sum, v, memory_order_relaxed);
            atomic_fetch_add_explicit(c->count, 1, memory_order_relaxed);
            continue;
        }
        if (mel_channel_status_closed(s) && atomic_load_explicit(c->stop, memory_order_acquire))
            break;
        mel_thread_yield();
    }
    return 0;
}

#define MPMC_PRODUCERS 4
#define MPMC_CONSUMERS 4
#define MPMC_PER       5000

MEL_TEST(channel, mpmc_stress_count_and_sum_invariant)
{
    Mel_Channel* drain = mel_channel_create(sizeof(int), 64, mel_alloc_heap());

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, MPMC_PRODUCERS + MPMC_CONSUMERS + 1));

    _Atomic(i64) sum = 0;
    _Atomic(i64) count = 0;
    _Atomic(i32) stop = 0;

    Mpmc_Producer prod[MPMC_PRODUCERS];
    Mel_Thread    prod_th[MPMC_PRODUCERS];
    i64           expect_sum = 0;
    i64           expect_count = (i64)MPMC_PRODUCERS * MPMC_PER;
    for (int t = 0; t < MPMC_PRODUCERS; t++)
    {
        prod[t] = (Mpmc_Producer){ .drain = drain, .start = &start, .first = t * MPMC_PER, .per = MPMC_PER, .done = 0 };
        for (int i = 0; i < MPMC_PER; i++)
            expect_sum += (i64)(t * MPMC_PER + i);
        MEL_REQUIRE(mel_thread_spawn(&prod_th[t], mpmc_producer_main, &prod[t], .name = "chan-prod"));
    }

    Mpmc_Consumer cons[MPMC_CONSUMERS];
    Mel_Thread    cons_th[MPMC_CONSUMERS];
    for (int t = 0; t < MPMC_CONSUMERS; t++)
    {
        cons[t] = (Mpmc_Consumer){ .drain = drain, .start = &start, .sum = &sum, .count = &count, .stop = &stop };
        MEL_REQUIRE(mel_thread_spawn(&cons_th[t], mpmc_consumer_main, &cons[t], .name = "chan-cons"));
    }

    mel_barrier_wait(&start);

    for (int t = 0; t < MPMC_PRODUCERS; t++)
    {
        int rc = 0;
        mel_thread_join(&prod_th[t], &rc);
    }

    for (i64 i = 0; i < CHAN_SPIN_LIMIT; i++)
    {
        if (atomic_load_explicit(&count, memory_order_acquire) == expect_count)
            break;
        mel_thread_yield();
    }

    atomic_store_explicit(&stop, 1, memory_order_release);
    mel_channel_close(drain);

    for (int t = 0; t < MPMC_CONSUMERS; t++)
    {
        int rc = 0;
        mel_thread_join(&cons_th[t], &rc);
    }

    MEL_EXPECT_EQ((i64)atomic_load(&count), expect_count);
    MEL_EXPECT_EQ((i64)atomic_load(&sum), expect_sum);

    mel_barrier_destroy(&start);
    mel_channel_destroy(drain);
}
