#include <channel/channel.h>
#include <test/test.h>

#include <allocator/heap.h>
#include <allocator/allocator.h>
#include <collection/list.h>
#include <executor/executor.h>
#include <future/future.h>
#include <thread/thread.h>
#include <thread/barrier.h>

#include <stdatomic.h>

#define RACE_SPIN_LIMIT 200000000

typedef struct
{
    Mel_Channel*  ch;
    Mel_Barrier*  start;
    _Atomic(i32)* go_close;
    _Atomic(i64)* ok;
    _Atomic(i64)* closed;
    _Atomic(i64)* err;
} Race_Future_Ctx;

static int race_future_recv_main(void* user)
{
    Race_Future_Ctx* c = (Race_Future_Ctx*)user;
    mel_barrier_wait(c->start);

    int        out = 0;
    Mel_Future f;
    mel_channel_recv_future(c->ch, &out, &f, mel_executor_inline(), mel_alloc_heap());

    atomic_store_explicit(c->go_close, 1, memory_order_release);

    for (i64 i = 0; i < RACE_SPIN_LIMIT; i++)
    {
        if (mel_future_resolved(&f))
            break;
        mel_thread_yield();
    }

    Mel_Future_Status s = mel_future_status(&f);
    if (mel_future_status_failed(s) && (s & MEL_FUTURE_BROKEN))
        atomic_fetch_add_explicit(c->closed, 1, memory_order_relaxed);
    else if (mel_future_status_failed(s))
        atomic_fetch_add_explicit(c->err, 1, memory_order_relaxed);
    else
        atomic_fetch_add_explicit(c->ok, 1, memory_order_relaxed);

    return 0;
}

MEL_TEST(channel_race, close_vs_parked_future_recv_single_outcome)
{
    const int rounds = 2000;

    _Atomic(i64) ok = 0;
    _Atomic(i64) closed = 0;
    _Atomic(i64) err = 0;

    for (int r = 0; r < rounds; r++)
    {
        Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

        Mel_Barrier start;
        MEL_REQUIRE(mel_barrier_init(&start, 2));

        _Atomic(i32)    go_close = 0;
        Race_Future_Ctx ctx = { .ch = ch, .start = &start, .go_close = &go_close, .ok = &ok, .closed = &closed, .err = &err };

        Mel_Thread th;
        MEL_REQUIRE(mel_thread_spawn(&th, race_future_recv_main, &ctx, .name = "race-recv"));

        mel_barrier_wait(&start);

        for (i64 i = 0; i < RACE_SPIN_LIMIT; i++)
        {
            if (atomic_load_explicit(&go_close, memory_order_acquire) == 1)
                break;
            mel_thread_yield();
        }
        mel_channel_close(ch);

        int rc = 0;
        mel_thread_join(&th, &rc);

        mel_barrier_destroy(&start);
        mel_channel_destroy(ch);
    }

    MEL_EXPECT_EQ((i64)atomic_load(&err), (i64)0);
    MEL_EXPECT_EQ((i64)atomic_load(&closed), (i64)rounds);
    MEL_EXPECT_EQ((i64)atomic_load(&ok), (i64)0);
}

typedef struct
{
    Mel_Channel* ch;
    Mel_Barrier* start;
    int          first;
    int          per;
    _Atomic(i32) done;
} Race_Producer;

static int race_producer_main(void* user)
{
    Race_Producer* p = (Race_Producer*)user;
    mel_barrier_wait(p->start);
    for (int i = 0; i < p->per; i++)
    {
        int v = p->first + i;
        for (;;)
        {
            if (mel_channel_try_send(p->ch, &v) == MEL_CHANNEL_OK)
                break;
            mel_thread_yield();
        }
    }
    atomic_store_explicit(&p->done, 1, memory_order_release);
    return 0;
}

typedef struct
{
    Mel_Channel*  ch;
    Mel_Barrier*  start;
    _Atomic(i64)* sum;
    _Atomic(i64)* count;
    i64           target;
    _Atomic(i32)  done;
} Race_Consumer;

static int race_consumer_main(void* user)
{
    Race_Consumer* c = (Race_Consumer*)user;
    mel_barrier_wait(c->start);
    for (;;)
    {
        int        out = 0;
        Mel_Future f;
        mel_channel_recv_future(c->ch, &out, &f, mel_executor_inline(), mel_alloc_heap());

        while (!mel_future_resolved(&f))
            mel_thread_yield();

        Mel_Future_Status s = mel_future_status(&f);
        if (mel_future_status_failed(s))
            break;

        atomic_fetch_add_explicit(c->sum, out, memory_order_relaxed);
        atomic_fetch_add_explicit(c->count, 1, memory_order_relaxed);
    }
    atomic_store_explicit(&c->done, 1, memory_order_release);
    return 0;
}

#define RACE_PRODUCERS 3
#define RACE_CONSUMERS 3
#define RACE_PER       3000

MEL_TEST(channel_race, mpmc_future_recv_count_and_sum_invariant)
{
    Mel_Channel* ch = mel_channel_create(sizeof(int), 0, mel_alloc_heap());

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, RACE_PRODUCERS + RACE_CONSUMERS + 1));

    _Atomic(i64) sum = 0;
    _Atomic(i64) count = 0;
    i64          target = (i64)RACE_PRODUCERS * RACE_PER;
    i64          expect_sum = 0;

    Race_Producer prod[RACE_PRODUCERS];
    Mel_Thread    prod_th[RACE_PRODUCERS];
    for (int t = 0; t < RACE_PRODUCERS; t++)
    {
        prod[t] = (Race_Producer){ .ch = ch, .start = &start, .first = t * RACE_PER, .per = RACE_PER, .done = 0 };
        for (int i = 0; i < RACE_PER; i++)
            expect_sum += (i64)(t * RACE_PER + i);
        MEL_REQUIRE(mel_thread_spawn(&prod_th[t], race_producer_main, &prod[t], .name = "race-prod"));
    }

    Race_Consumer cons[RACE_CONSUMERS];
    Mel_Thread    cons_th[RACE_CONSUMERS];
    for (int t = 0; t < RACE_CONSUMERS; t++)
    {
        cons[t] = (Race_Consumer){ .ch = ch, .start = &start, .sum = &sum, .count = &count, .target = target, .done = 0 };
        MEL_REQUIRE(mel_thread_spawn(&cons_th[t], race_consumer_main, &cons[t], .name = "race-cons"));
    }

    mel_barrier_wait(&start);

    for (int t = 0; t < RACE_PRODUCERS; t++)
    {
        int rc = 0;
        mel_thread_join(&prod_th[t], &rc);
    }

    for (i64 i = 0; i < RACE_SPIN_LIMIT; i++)
    {
        if (atomic_load_explicit(&count, memory_order_acquire) >= target)
            break;
        mel_thread_yield();
    }

    mel_channel_close(ch);

    for (int t = 0; t < RACE_CONSUMERS; t++)
    {
        int rc = 0;
        mel_thread_join(&cons_th[t], &rc);
    }

    MEL_EXPECT_EQ((i64)atomic_load(&count), target);
    MEL_EXPECT_EQ((i64)atomic_load(&sum), expect_sum);

    mel_barrier_destroy(&start);
    mel_channel_destroy(ch);
}
