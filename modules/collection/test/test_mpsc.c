#include <test/test.h>

#include <collection.mpsc/mpsc.h>
#include <collection.list/list.h>
#include <thread/thread.h>
#include <thread/barrier.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <stdatomic.h>

#define MPSC_FIFO_N           1024
#define MPSC_INTERLEAVE_N     256
#define MPSC_INTERLEAVE_GAP   3
#define MPSC_STRESS_PRODUCERS 8
#define MPSC_STRESS_PER       100000
#define MPSC_SINGLE_N         200000
#define MPSC_OVERSUB_MULT     4
#define MPSC_OVERSUB_PER      50000
#define MPSC_RACE_ROUNDS      200000
#define MPSC_REUSE_NODES      64
#define MPSC_REUSE_CYCLES     50000

typedef struct Item
{
    Mel_Mpsc_Node node;
    u64           producer;
    u64           seq;
    u64           value;
} Item;

static Item* item_of(Mel_Mpsc_Node* n) { return mel_container_of(n, Item, node); }

static u64 mix(u64 producer, u64 seq) { return (producer << 40) ^ (seq * 2654435761u) ^ (seq + 1); }

MEL_TEST(mpsc, empty_pop_returns_null)
{
    Mel_Mpsc q;
    mel_mpsc_init(&q);

    MEL_EXPECT_NULL(mel_mpsc_pop(&q));
    MEL_EXPECT_NULL(mel_mpsc_pop(&q));
}

MEL_TEST(mpsc, single_element_roundtrip)
{
    Mel_Mpsc q;
    mel_mpsc_init(&q);

    Item it = { .value = 42 };
    mel_mpsc_push(&q, &it.node);

    Mel_Mpsc_Node* got = mel_mpsc_pop(&q);
    MEL_REQUIRE_NOT_NULL(got);
    MEL_EXPECT_EQ(item_of(got), &it);
    MEL_EXPECT_EQ(item_of(got)->value, 42u);

    MEL_EXPECT_NULL(mel_mpsc_pop(&q));
}

MEL_TEST(mpsc, single_thread_fifo_order)
{
    Mel_Mpsc q;
    mel_mpsc_init(&q);

    const Mel_Alloc* alloc = mel_alloc_heap();
    Item*            items = mel_alloc(alloc, sizeof(Item) * MPSC_FIFO_N);
    MEL_REQUIRE_NOT_NULL(items);

    for (u64 i = 0; i < MPSC_FIFO_N; i++)
    {
        items[i].value = i;
        mel_mpsc_push(&q, &items[i].node);
    }

    for (u64 i = 0; i < MPSC_FIFO_N; i++)
    {
        Mel_Mpsc_Node* got = mel_mpsc_pop(&q);
        MEL_REQUIRE_NOT_NULL(got);
        MEL_EXPECT_EQ(item_of(got)->value, i);
    }
    MEL_EXPECT_NULL(mel_mpsc_pop(&q));

    mel_dealloc(alloc, items);
}

MEL_TEST(mpsc, interleaved_push_pop_fifo)
{
    Mel_Mpsc q;
    mel_mpsc_init(&q);

    const Mel_Alloc* alloc = mel_alloc_heap();
    Item*            items = mel_alloc(alloc, sizeof(Item) * MPSC_INTERLEAVE_N);
    MEL_REQUIRE_NOT_NULL(items);

    u64 push_i = 0;
    u64 pop_i = 0;
    while (pop_i < MPSC_INTERLEAVE_N)
    {
        if (push_i < MPSC_INTERLEAVE_N && (push_i - pop_i) < MPSC_INTERLEAVE_GAP)
        {
            items[push_i].value = push_i;
            mel_mpsc_push(&q, &items[push_i].node);
            push_i++;
            continue;
        }
        Mel_Mpsc_Node* got = mel_mpsc_pop(&q);
        MEL_REQUIRE_NOT_NULL(got);
        MEL_EXPECT_EQ(item_of(got)->value, pop_i);
        pop_i++;
    }
    MEL_EXPECT_NULL(mel_mpsc_pop(&q));

    mel_dealloc(alloc, items);
}

typedef struct Producer_Ctx
{
    Mel_Mpsc*    q;
    Mel_Barrier* start;
    u64          producer_id;
    u64          count;
    Item*        items;
} Producer_Ctx;

static int producer_main(void* user)
{
    Producer_Ctx* ctx = (Producer_Ctx*)user;
    mel_barrier_wait(ctx->start);

    for (u64 i = 0; i < ctx->count; i++)
    {
        Item* it = &ctx->items[i];
        it->producer = ctx->producer_id;
        it->seq = i;
        it->value = mix(ctx->producer_id, i);
        mel_mpsc_push(ctx->q, &it->node);
    }
    return 0;
}

MEL_TEST(mpsc, mp_sc_stress_count_and_checksum)
{
    const u64 producers = MPSC_STRESS_PRODUCERS;
    const u64 per = MPSC_STRESS_PER;
    const u64 total = producers * per;

    Mel_Mpsc q;
    mel_mpsc_init(&q);

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, (u32)producers + 1));

    const Mel_Alloc* alloc = mel_alloc_heap();

    Item**        blocks = mel_alloc(alloc, sizeof(Item*) * producers);
    Producer_Ctx* ctx = mel_alloc(alloc, sizeof(Producer_Ctx) * producers);
    Mel_Thread*   threads = mel_alloc(alloc, sizeof(Mel_Thread) * producers);
    u64*          last_seq = mel_alloc(alloc, sizeof(u64) * producers);
    bool*         seen = mel_alloc(alloc, sizeof(bool) * producers);
    MEL_REQUIRE_NOT_NULL(blocks);
    MEL_REQUIRE_NOT_NULL(ctx);
    MEL_REQUIRE_NOT_NULL(threads);
    MEL_REQUIRE_NOT_NULL(last_seq);
    MEL_REQUIRE_NOT_NULL(seen);

    for (u64 p = 0; p < producers; p++)
    {
        blocks[p] = mel_alloc(alloc, sizeof(Item) * per);
        MEL_REQUIRE_NOT_NULL(blocks[p]);
        last_seq[p] = 0;
        seen[p] = false;
    }

    for (u64 p = 0; p < producers; p++)
    {
        ctx[p] = (Producer_Ctx){ .q = &q, .start = &start, .producer_id = p, .count = per, .items = blocks[p] };
        MEL_REQUIRE(mel_thread_spawn(&threads[p], producer_main, &ctx[p], .name = "mpsc-prod"));
    }

    u64 expect_sum = 0;
    for (u64 p = 0; p < producers; p++)
        for (u64 i = 0; i < per; i++)
            expect_sum ^= mix(p, i);

    mel_barrier_wait(&start);

    u64  got_count = 0;
    u64  got_sum = 0;
    bool order_ok = true;
    while (got_count < total)
    {
        Mel_Mpsc_Node* n = mel_mpsc_pop(&q);
        if (n == NULL)
        {
            mel_thread_yield();
            continue;
        }
        Item* it = item_of(n);
        got_count++;
        got_sum ^= it->value;

        u64 pid = it->producer;
        if (seen[pid] && it->seq != last_seq[pid] + 1)
            order_ok = false;
        if (!seen[pid] && it->seq != 0)
            order_ok = false;
        last_seq[pid] = it->seq;
        seen[pid] = true;
    }

    for (u64 p = 0; p < producers; p++)
    {
        int rc = 0;
        mel_thread_join(&threads[p], &rc);
    }

    MEL_EXPECT_NULL(mel_mpsc_pop(&q));
    MEL_EXPECT_EQ(got_count, total);
    MEL_EXPECT_EQ(got_sum, expect_sum);
    MEL_EXPECT(order_ok);

    for (u64 p = 0; p < producers; p++)
    {
        MEL_EXPECT(seen[p]);
        MEL_EXPECT_EQ(last_seq[p], per - 1);
    }

    for (u64 p = 0; p < producers; p++)
        mel_dealloc(alloc, blocks[p]);
    mel_dealloc(alloc, seen);
    mel_dealloc(alloc, last_seq);
    mel_dealloc(alloc, threads);
    mel_dealloc(alloc, ctx);
    mel_dealloc(alloc, blocks);
    mel_barrier_destroy(&start);
}

MEL_TEST(mpsc, single_producer_visible_to_consumer)
{
    const u64 n = MPSC_SINGLE_N;

    Mel_Mpsc q;
    mel_mpsc_init(&q);

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, 2));

    const Mel_Alloc* alloc = mel_alloc_heap();
    Item*            items = mel_alloc(alloc, sizeof(Item) * n);
    MEL_REQUIRE_NOT_NULL(items);

    Producer_Ctx ctx = { .q = &q, .start = &start, .producer_id = 0, .count = n, .items = items };
    Mel_Thread   th;
    MEL_REQUIRE(mel_thread_spawn(&th, producer_main, &ctx, .name = "mpsc-1p"));

    mel_barrier_wait(&start);

    u64  got = 0;
    u64  next = 0;
    bool order_ok = true;
    while (got < n)
    {
        Mel_Mpsc_Node* node = mel_mpsc_pop(&q);
        if (node == NULL)
        {
            mel_thread_yield();
            continue;
        }
        Item* it = item_of(node);
        if (it->seq != next)
            order_ok = false;
        next++;
        got++;
    }

    int rc = 0;
    mel_thread_join(&th, &rc);

    MEL_EXPECT_EQ(got, n);
    MEL_EXPECT(order_ok);
    MEL_EXPECT_NULL(mel_mpsc_pop(&q));

    mel_dealloc(alloc, items);
    mel_barrier_destroy(&start);
}

MEL_TEST(mpsc, drain_reuse_after_empty)
{
    Mel_Mpsc q;
    mel_mpsc_init(&q);

    Item a = { .value = 1 };
    Item b = { .value = 2 };
    Item c = { .value = 3 };

    mel_mpsc_push(&q, &a.node);
    MEL_REQUIRE_NOT_NULL(mel_mpsc_pop(&q));
    MEL_EXPECT_NULL(mel_mpsc_pop(&q));

    mel_mpsc_push(&q, &b.node);
    mel_mpsc_push(&q, &c.node);
    MEL_EXPECT_EQ(item_of(mel_mpsc_pop(&q))->value, 2u);
    MEL_EXPECT_EQ(item_of(mel_mpsc_pop(&q))->value, 3u);
    MEL_EXPECT_NULL(mel_mpsc_pop(&q));
}

MEL_TEST(mpsc, oversubscribed_producers)
{
    const u64 producers = (u64)mel_thread_hardware_concurrency() * MPSC_OVERSUB_MULT;
    const u64 per = MPSC_OVERSUB_PER;
    const u64 total = producers * per;

    Mel_Mpsc q;
    mel_mpsc_init(&q);

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, (u32)producers + 1));

    const Mel_Alloc* alloc = mel_alloc_heap();

    Item**        blocks = mel_alloc(alloc, sizeof(Item*) * producers);
    Producer_Ctx* ctx = mel_alloc(alloc, sizeof(Producer_Ctx) * producers);
    Mel_Thread*   threads = mel_alloc(alloc, sizeof(Mel_Thread) * producers);
    u64*          last_seq = mel_alloc(alloc, sizeof(u64) * producers);
    bool*         seen = mel_alloc(alloc, sizeof(bool) * producers);
    MEL_REQUIRE_NOT_NULL(blocks);
    MEL_REQUIRE_NOT_NULL(ctx);
    MEL_REQUIRE_NOT_NULL(threads);
    MEL_REQUIRE_NOT_NULL(last_seq);
    MEL_REQUIRE_NOT_NULL(seen);

    for (u64 p = 0; p < producers; p++)
    {
        blocks[p] = mel_alloc(alloc, sizeof(Item) * per);
        MEL_REQUIRE_NOT_NULL(blocks[p]);
        last_seq[p] = 0;
        seen[p] = false;
    }

    for (u64 p = 0; p < producers; p++)
    {
        ctx[p] = (Producer_Ctx){ .q = &q, .start = &start, .producer_id = p, .count = per, .items = blocks[p] };
        MEL_REQUIRE(mel_thread_spawn(&threads[p], producer_main, &ctx[p], .name = "mpsc-over"));
    }

    u64 expect_sum = 0;
    for (u64 p = 0; p < producers; p++)
        for (u64 i = 0; i < per; i++)
            expect_sum ^= mix(p, i);

    mel_barrier_wait(&start);

    u64  got_count = 0;
    u64  got_sum = 0;
    bool order_ok = true;
    while (got_count < total)
    {
        Mel_Mpsc_Node* n = mel_mpsc_pop(&q);
        if (n == NULL)
        {
            mel_thread_yield();
            continue;
        }
        Item* it = item_of(n);
        got_count++;
        got_sum ^= it->value;

        u64 pid = it->producer;
        if (seen[pid] && it->seq != last_seq[pid] + 1)
            order_ok = false;
        if (!seen[pid] && it->seq != 0)
            order_ok = false;
        last_seq[pid] = it->seq;
        seen[pid] = true;
    }

    for (u64 p = 0; p < producers; p++)
    {
        int rc = 0;
        mel_thread_join(&threads[p], &rc);
    }

    MEL_EXPECT_NULL(mel_mpsc_pop(&q));
    MEL_EXPECT_EQ(got_count, total);
    MEL_EXPECT_EQ(got_sum, expect_sum);
    MEL_EXPECT(order_ok);

    for (u64 p = 0; p < producers; p++)
    {
        MEL_EXPECT(seen[p]);
        MEL_EXPECT_EQ(last_seq[p], per - 1);
    }

    for (u64 p = 0; p < producers; p++)
        mel_dealloc(alloc, blocks[p]);
    mel_dealloc(alloc, seen);
    mel_dealloc(alloc, last_seq);
    mel_dealloc(alloc, threads);
    mel_dealloc(alloc, ctx);
    mel_dealloc(alloc, blocks);
    mel_barrier_destroy(&start);
}

typedef struct Race_Ctx
{
    Mel_Mpsc*     q;
    _Atomic(u64)* gate;
    Item*         slots;
    u64           rounds;
} Race_Ctx;

static int race_producer_main(void* user)
{
    Race_Ctx* ctx = (Race_Ctx*)user;
    for (u64 r = 0; r < ctx->rounds; r++)
    {
        while (atomic_load_explicit(ctx->gate, memory_order_acquire) != (r * 2))
            mel_thread_yield();

        Item* it = &ctx->slots[r & 1];
        it->seq = r;
        it->value = mix(7, r);
        mel_mpsc_push(ctx->q, &it->node);

        atomic_store_explicit(ctx->gate, r * 2 + 1, memory_order_release);
    }
    return 0;
}

MEL_TEST(mpsc, pop_races_last_push)
{
    Mel_Mpsc q;
    mel_mpsc_init(&q);

    _Atomic(u64) gate;
    atomic_store_explicit(&gate, 0, memory_order_relaxed);

    Item slots[2] = { 0 };

    Race_Ctx   ctx = { .q = &q, .gate = &gate, .slots = slots, .rounds = MPSC_RACE_ROUNDS };
    Mel_Thread th;
    MEL_REQUIRE(mel_thread_spawn(&th, race_producer_main, &ctx, .name = "mpsc-race"));

    bool ok = true;
    for (u64 r = 0; r < MPSC_RACE_ROUNDS; r++)
    {
        atomic_store_explicit(&gate, r * 2, memory_order_release);

        Mel_Mpsc_Node* node = NULL;
        for (;;)
        {
            node = mel_mpsc_pop(&q);
            if (node != NULL)
                break;
            mel_thread_yield();
        }

        Item* it = item_of(node);
        if (it->seq != r || it->value != mix(7, r))
            ok = false;

        while (atomic_load_explicit(&gate, memory_order_acquire) != (r * 2 + 1))
            mel_thread_yield();

        MEL_EXPECT_NULL(mel_mpsc_pop(&q));
    }

    int rc = 0;
    mel_thread_join(&th, &rc);

    MEL_EXPECT(ok);
    MEL_EXPECT_NULL(mel_mpsc_pop(&q));
}

MEL_TEST(mpsc, drain_reuse_many_cycles)
{
    Mel_Mpsc q;
    mel_mpsc_init(&q);

    const Mel_Alloc* alloc = mel_alloc_heap();
    Item*            items = mel_alloc(alloc, sizeof(Item) * MPSC_REUSE_NODES);
    MEL_REQUIRE_NOT_NULL(items);

    bool ok = true;
    for (u64 cycle = 0; cycle < MPSC_REUSE_CYCLES; cycle++)
    {
        for (u64 i = 0; i < MPSC_REUSE_NODES; i++)
        {
            items[i].value = cycle * MPSC_REUSE_NODES + i;
            mel_mpsc_push(&q, &items[i].node);
        }
        for (u64 i = 0; i < MPSC_REUSE_NODES; i++)
        {
            Mel_Mpsc_Node* got = mel_mpsc_pop(&q);
            if (got == NULL || item_of(got)->value != cycle * MPSC_REUSE_NODES + i)
                ok = false;
        }
        if (mel_mpsc_pop(&q) != NULL)
            ok = false;
    }

    MEL_EXPECT(ok);
    MEL_EXPECT_NULL(mel_mpsc_pop(&q));

    mel_dealloc(alloc, items);
}

typedef struct Skewed
{
    u64           pad_a;
    u8            wedge;
    u64           pad_b[3];
    Mel_Mpsc_Node node;
    u64           value;
} Skewed;

static Skewed* skewed_of(Mel_Mpsc_Node* n) { return mel_container_of(n, Skewed, node); }

MEL_TEST(mpsc, container_of_with_offset_node)
{
    MEL_REQUIRE(offsetof(Skewed, node) != 0);

    Mel_Mpsc q;
    mel_mpsc_init(&q);

    const Mel_Alloc* alloc = mel_alloc_heap();
    Skewed*          items = mel_alloc(alloc, sizeof(Skewed) * MPSC_FIFO_N);
    MEL_REQUIRE_NOT_NULL(items);

    for (u64 i = 0; i < MPSC_FIFO_N; i++)
    {
        items[i].wedge = (u8)(i & 0xFF);
        items[i].value = i * 3 + 1;
        mel_mpsc_push(&q, &items[i].node);
    }

    for (u64 i = 0; i < MPSC_FIFO_N; i++)
    {
        Mel_Mpsc_Node* got = mel_mpsc_pop(&q);
        MEL_REQUIRE_NOT_NULL(got);
        Skewed* s = skewed_of(got);
        MEL_EXPECT_EQ(s, &items[i]);
        MEL_EXPECT_EQ(s->value, i * 3 + 1);
        MEL_EXPECT_EQ((u64)s->wedge, (u64)(u8)(i & 0xFF));
    }
    MEL_EXPECT_NULL(mel_mpsc_pop(&q));

    mel_dealloc(alloc, items);
}
