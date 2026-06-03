#include <event/event.h>
#include <executor/executor.h>
#include <test/test.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.list/list.h>
#include <collection.mpsc/mpsc.h>
#include <thread/thread.h>

#include <string.h>

typedef struct
{
    int received[64];
    int count;
} Sink;

static void sink_cb(const void* item, void* user)
{
    Sink* s = (Sink*)user;
    if (s->count < 64)
        s->received[s->count] = *(const int*)item;
    s->count++;
}

static void noop_overflow(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(info);
    MEL_UNUSED(user);
}

MEL_TEST(event, push_delivery_runs_callback)
{
    Mel_Event* ev = mel_event_create(mel_alloc_heap(), sizeof(int), 8, mel_event_policy_lossless(noop_overflow, nullptr));
    MEL_REQUIRE_NOT_NULL(ev);

    Sink          s = { 0 };
    Mel_Executor* exec = mel_executor_inline();
    Mel_Event_Sub sub = mel_event_subscribe_push(ev, exec, sink_cb, &s);

    int v = 42;
    mel_event_fire(ev, &v);

    MEL_EXPECT_EQ(s.count, 1);
    MEL_EXPECT_EQ(s.received[0], 42);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
}

MEL_TEST(event, push_delivery_preserves_order)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 16, mel_event_policy_lossless(noop_overflow, nullptr));
    Sink          s = { 0 };
    Mel_Event_Sub sub = mel_event_subscribe_push(ev, mel_executor_inline(), sink_cb, &s);

    for (int i = 0; i < 5; i++)
        mel_event_fire(ev, &i);

    MEL_REQUIRE_EQ(s.count, 5);
    for (int i = 0; i < 5; i++)
        MEL_EXPECT_EQ(s.received[i], i);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
}

MEL_TEST(event, pull_drain_returns_items_in_order)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 8, mel_event_policy_lossless(noop_overflow, nullptr));
    Mel_Event_Sub sub = mel_event_subscribe_pull(ev, nullptr);

    for (int i = 10; i < 14; i++)
        mel_event_fire(ev, &i);

    MEL_EXPECT_EQ(mel_event_pull_pending(ev, sub), 4u);

    int out = 0;
    for (int i = 10; i < 14; i++)
    {
        MEL_REQUIRE(mel_event_pull(ev, sub, &out));
        MEL_EXPECT_EQ(out, i);
    }
    MEL_EXPECT(!mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(mel_event_pull_pending(ev, sub), 0u);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
}

MEL_TEST(event, pull_does_not_deliver_via_executor)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 8, mel_event_policy_lossless(noop_overflow, nullptr));
    Mel_Event_Sub sub = mel_event_subscribe_pull(ev, nullptr);

    int v = 7;
    mel_event_fire(ev, &v);
    MEL_EXPECT_EQ(mel_event_pull_pending(ev, sub), 1u);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
}

MEL_TEST(event, fanout_delivers_to_all_live_subscribers)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 8, mel_event_policy_lossless(noop_overflow, nullptr));
    Mel_Executor* exec = mel_executor_inline();

    Sink          a = { 0 }, b = { 0 }, c = { 0 };
    Mel_Event_Sub sa = mel_event_subscribe_push(ev, exec, sink_cb, &a);
    Mel_Event_Sub sb = mel_event_subscribe_push(ev, exec, sink_cb, &b);
    Mel_Event_Sub sd = mel_event_subscribe_pull(ev, nullptr);

    MEL_EXPECT_EQ(mel_event_subscriber_count(ev), 3u);

    int v = 99;
    mel_event_fire(ev, &v);

    MEL_EXPECT_EQ(a.count, 1);
    MEL_EXPECT_EQ(a.received[0], 99);
    MEL_EXPECT_EQ(b.count, 1);
    MEL_EXPECT_EQ(b.received[0], 99);
    MEL_EXPECT_EQ(mel_event_pull_pending(ev, sd), 1u);

    MEL_UNUSED(c);
    mel_event_unsubscribe(ev, sa);
    mel_event_unsubscribe(ev, sb);
    mel_event_unsubscribe(ev, sd);
    mel_event_destroy(ev);
}

MEL_TEST(event, fanout_skips_unsubscribed)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 8, mel_event_policy_lossless(noop_overflow, nullptr));
    Mel_Executor* exec = mel_executor_inline();

    Sink          a = { 0 }, b = { 0 };
    Mel_Event_Sub sa = mel_event_subscribe_push(ev, exec, sink_cb, &a);
    Mel_Event_Sub sb = mel_event_subscribe_push(ev, exec, sink_cb, &b);

    mel_event_unsubscribe(ev, sa);
    MEL_EXPECT_EQ(mel_event_subscriber_count(ev), 1u);

    int v = 5;
    mel_event_fire(ev, &v);

    MEL_EXPECT_EQ(a.count, 0);
    MEL_EXPECT_EQ(b.count, 1);

    mel_event_unsubscribe(ev, sb);
    mel_event_destroy(ev);
}

typedef struct
{
    Mel_Event*    ev;
    Mel_Event_Sub self;
    int           seen;
} Self_Unsub;

static void self_unsub_cb(const void* item, void* user)
{
    Self_Unsub* su = (Self_Unsub*)user;
    su->seen = *(const int*)item;
    mel_event_unsubscribe(su->ev, su->self);
}

MEL_TEST(event, unsubscribe_during_fire_is_safe)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 8, mel_event_policy_lossless(noop_overflow, nullptr));
    Mel_Executor* exec = mel_executor_inline();

    Self_Unsub su = { 0 };
    su.ev = ev;
    su.self = mel_event_subscribe_push(ev, exec, self_unsub_cb, &su);

    MEL_EXPECT_EQ(mel_event_subscriber_count(ev), 1u);

    int v = 1234;
    mel_event_fire(ev, &v);

    MEL_EXPECT_EQ(su.seen, 1234);
    MEL_EXPECT_EQ(mel_event_subscriber_count(ev), 0u);

    int v2 = 5678;
    mel_event_fire(ev, &v2);
    MEL_EXPECT_EQ(su.seen, 1234);

    mel_event_destroy(ev);
}

static _Atomic(i64) g_poison_live;

static void* poison_cb(void* ptr, usize size, u32 align, const char* file, const char* func, u32 line, void* user_data)
{
    const Mel_Alloc* heap = mel_alloc_heap();
    MEL_UNUSED(user_data);

    if (ptr == NULL)
    {
        void* p = heap->alloc_cb(NULL, size, align, file, func, line, heap->user_data);
        if (p)
            atomic_fetch_add_explicit(&g_poison_live, 1, memory_order_relaxed);
        return p;
    }

    if (size == 0)
    {
        memset(ptr, 0xDD, 1);
        heap->alloc_cb(ptr, 0, align, file, func, line, heap->user_data);
        atomic_fetch_sub_explicit(&g_poison_live, 1, memory_order_relaxed);
        return NULL;
    }

    return heap->alloc_cb(ptr, size, align, file, func, line, heap->user_data);
}

typedef struct
{
    Mel_Event*    ev;
    Mel_Event_Sub self;
    int           seen;
} Poison_Unsub;

static void poison_unsub_cb(const void* item, void* user)
{
    Poison_Unsub* pu = (Poison_Unsub*)user;
    pu->seen = *(const int*)item;
    mel_event_unsubscribe(pu->ev, pu->self);
}

MEL_TEST(event, unsubscribe_during_fire_no_uaf_under_poison)
{
    atomic_store(&g_poison_live, 0);
    Mel_Alloc poison = { .alloc_cb = poison_cb, .user_data = NULL };

    Mel_Event*    ev = mel_event_create(&poison, sizeof(int), 8, mel_event_policy_lossless(noop_overflow, nullptr));
    Mel_Executor* exec = mel_executor_inline();

    Poison_Unsub pu = { 0 };
    pu.ev = ev;
    pu.self = mel_event_subscribe_push(ev, exec, poison_unsub_cb, &pu);

    Sink          survivor = { 0 };
    Mel_Event_Sub keep = mel_event_subscribe_push(ev, exec, sink_cb, &survivor);

    int v = 0x5151;
    mel_event_fire(ev, &v);

    MEL_EXPECT_EQ(pu.seen, 0x5151);
    MEL_EXPECT_EQ(survivor.count, 1);
    MEL_EXPECT_EQ(survivor.received[0], 0x5151);
    MEL_EXPECT_EQ(mel_event_subscriber_count(ev), 1u);

    int v2 = 0x6262;
    mel_event_fire(ev, &v2);
    MEL_EXPECT_EQ(pu.seen, 0x5151);
    MEL_EXPECT_EQ(survivor.count, 2);
    MEL_EXPECT_EQ(survivor.received[1], 0x6262);

    mel_event_unsubscribe(ev, keep);
    mel_event_destroy(ev);

    MEL_EXPECT_EQ((i64)atomic_load(&g_poison_live), (i64)0);
}

MEL_TEST(event, policy_latest_keeps_newest_drops_oldest)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 2, mel_event_policy_latest(noop_overflow, nullptr));
    Mel_Event_Sub sub = mel_event_subscribe_pull(ev, nullptr);

    for (int i = 1; i <= 5; i++)
        mel_event_fire(ev, &i);

    MEL_EXPECT_EQ(mel_event_pull_pending(ev, sub), 2u);

    int out = 0;
    MEL_REQUIRE(mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(out, 4);
    MEL_REQUIRE(mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(out, 5);

    MEL_EXPECT_EQ(mel_event_lag(ev, sub), (u64)3);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
}

MEL_TEST(event, policy_lossy_keeps_oldest_drops_newest_reports_lag)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 2, mel_event_policy_lossy_lag(noop_overflow, nullptr));
    Mel_Event_Sub sub = mel_event_subscribe_pull(ev, nullptr);

    for (int i = 1; i <= 5; i++)
        mel_event_fire(ev, &i);

    MEL_EXPECT_EQ(mel_event_pull_pending(ev, sub), 2u);

    int out = 0;
    MEL_REQUIRE(mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(out, 1);
    MEL_REQUIRE(mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(out, 2);

    MEL_EXPECT_EQ(mel_event_lag(ev, sub), (u64)3);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
}

typedef struct
{
    int backpressure_reports;
    u32 last_ring_count;
} Lossless_Watch;

static void lossless_overflow(const Mel_Event_Overflow_Info* info, void* user)
{
    Lossless_Watch* w = (Lossless_Watch*)user;
    if (info->backpressured)
        w->backpressure_reports++;
    w->last_ring_count = info->ring_count;
}

MEL_TEST(event, policy_lossless_backpressures_does_not_drop_oldest)
{
    Lossless_Watch w = { 0 };
    Mel_Event*     ev = mel_event_create(mel_alloc_heap(), sizeof(int), 2, mel_event_policy_lossless(lossless_overflow, &w));
    Mel_Event_Sub  sub = mel_event_subscribe_pull(ev, nullptr);

    for (int i = 1; i <= 5; i++)
        mel_event_fire(ev, &i);

    MEL_EXPECT_EQ(mel_event_pull_pending(ev, sub), 2u);
    MEL_EXPECT_EQ(w.backpressure_reports, 3);
    MEL_EXPECT_EQ(w.last_ring_count, 2u);

    int out = 0;
    MEL_REQUIRE(mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(out, 1);
    MEL_REQUIRE(mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(out, 2);

    MEL_EXPECT_EQ(mel_event_lag(ev, sub), (u64)3);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
}

MEL_TEST(event, policy_lossless_recovers_after_drain)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 2, mel_event_policy_lossless(noop_overflow, nullptr));
    Mel_Event_Sub sub = mel_event_subscribe_pull(ev, nullptr);

    int a = 1, b = 2, c = 3;
    mel_event_fire(ev, &a);
    mel_event_fire(ev, &b);
    mel_event_fire(ev, &c);
    MEL_EXPECT_EQ(mel_event_lag(ev, sub), (u64)1);

    int out = 0;
    MEL_REQUIRE(mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(out, 1);

    int d = 4;
    mel_event_fire(ev, &d);
    MEL_EXPECT_EQ(mel_event_lag(ev, sub), (u64)1);
    MEL_EXPECT_EQ(mel_event_pull_pending(ev, sub), 2u);

    MEL_REQUIRE(mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(out, 2);
    MEL_REQUIRE(mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(out, 4);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
}

MEL_TEST(event, push_drains_buffered_burst_in_order)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 32, mel_event_policy_lossless(noop_overflow, nullptr));
    Sink          s = { 0 };
    Mel_Event_Sub sub = mel_event_subscribe_push(ev, mel_executor_inline(), sink_cb, &s);

    for (int i = 0; i < 20; i++)
        mel_event_fire(ev, &i);

    MEL_REQUIRE_EQ(s.count, 20);
    for (int i = 0; i < 20; i++)
        MEL_EXPECT_EQ(s.received[i], i);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
}

static _Atomic(i64) g_count_live;

static void* counting_cb(void* ptr, usize size, u32 align, const char* file, const char* func, u32 line, void* user_data)
{
    const Mel_Alloc* heap = mel_alloc_heap();
    MEL_UNUSED(user_data);

    if (ptr == NULL)
    {
        void* p = heap->alloc_cb(NULL, size, align, file, func, line, heap->user_data);
        if (p)
            atomic_fetch_add_explicit(&g_count_live, 1, memory_order_relaxed);
        return p;
    }
    if (size == 0)
    {
        heap->alloc_cb(ptr, 0, align, file, func, line, heap->user_data);
        atomic_fetch_sub_explicit(&g_count_live, 1, memory_order_relaxed);
        return NULL;
    }
    return heap->alloc_cb(ptr, size, align, file, func, line, heap->user_data);
}

MEL_TEST(event, steady_path_no_per_fire_alloc)
{
    atomic_store(&g_count_live, 0);
    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Event*    ev = mel_event_create(&counting, sizeof(int), 8, mel_event_policy_latest(noop_overflow, nullptr));
    Sink          s = { 0 };
    Mel_Event_Sub sub = mel_event_subscribe_push(ev, mel_executor_inline(), sink_cb, &s);

    i64 baseline = atomic_load(&g_count_live);
    for (int i = 0; i < 1000; i++)
        mel_event_fire(ev, &i);
    i64 after = atomic_load(&g_count_live);

    MEL_EXPECT_EQ(after, baseline);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
    MEL_EXPECT_EQ((i64)atomic_load(&g_count_live), (i64)0);
}

static void custom_drop_oldest(Mel_Event_Overflow_Info* info, void* user)
{
    int* calls = (int*)user;
    (*calls)++;
    info->drop_oldest = true;
    info->accepted = true;
}

MEL_TEST(event, policy_custom_open_encoding)
{
    int           calls = 0;
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 2, mel_event_policy_custom(custom_drop_oldest, nullptr, &calls));
    Mel_Event_Sub sub = mel_event_subscribe_pull(ev, nullptr);

    for (int i = 1; i <= 4; i++)
        mel_event_fire(ev, &i);

    MEL_EXPECT_EQ(calls, 2);
    int out = 0;
    MEL_REQUIRE(mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(out, 3);
    MEL_REQUIRE(mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(out, 4);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
}

MEL_TEST(event, stale_handle_resolves_to_nothing)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 4, mel_event_policy_lossless(noop_overflow, nullptr));
    Mel_Event_Sub sub = mel_event_subscribe_pull(ev, nullptr);

    int v = 1;
    mel_event_fire(ev, &v);
    mel_event_unsubscribe(ev, sub);

    int out = 0;
    MEL_EXPECT(!mel_event_pull(ev, sub, &out));
    MEL_EXPECT_EQ(mel_event_pull_pending(ev, sub), 0u);
    MEL_EXPECT_EQ(mel_event_lag(ev, sub), (u64)0);

    mel_event_unsubscribe(ev, sub);

    mel_event_destroy(ev);
}

MEL_TEST(event, reused_slot_distinct_generation)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 4, mel_event_policy_lossless(noop_overflow, nullptr));
    Mel_Event_Sub a = mel_event_subscribe_pull(ev, nullptr);
    mel_event_unsubscribe(ev, a);

    Mel_Event_Sub b = mel_event_subscribe_pull(ev, nullptr);
    int           v = 9;
    mel_event_fire(ev, &v);

    MEL_EXPECT_EQ(mel_event_pull_pending(ev, b), 1u);
    MEL_EXPECT_EQ(mel_event_pull_pending(ev, a), 0u);
    MEL_EXPECT_NEQ(a.handle.generation, b.handle.generation);

    mel_event_unsubscribe(ev, b);
    mel_event_destroy(ev);
}

typedef struct
{
    Mel_Event*    ev;
    Mel_Event_Sub victim;
    int           seen;
} Sibling_Killer;

static void sibling_killer_cb(const void* item, void* user)
{
    Sibling_Killer* k = (Sibling_Killer*)user;
    k->seen = *(const int*)item;
    mel_event_unsubscribe(k->ev, k->victim);
}

MEL_TEST(event, sibling_unsubscribe_during_fire_is_safe)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 8, mel_event_policy_latest(noop_overflow, nullptr));
    Mel_Executor* exec = mel_executor_inline();

    Sibling_Killer killer = { 0 };
    killer.ev = ev;

    Sink victim = { 0 };
    Sink tail = { 0 };

    mel_event_subscribe_push(ev, exec, sibling_killer_cb, &killer);
    killer.victim = mel_event_subscribe_push(ev, exec, sink_cb, &victim);
    mel_event_subscribe_push(ev, exec, sink_cb, &tail);

    int v = 7;
    mel_event_fire(ev, &v);

    MEL_EXPECT_EQ(killer.seen, 7);
    MEL_EXPECT_EQ(tail.count, 1);
    MEL_EXPECT_EQ(mel_event_subscriber_count(ev), 2u);

    mel_event_destroy(ev);
}

typedef struct
{
    Mel_Event*    ev;
    Mel_Event_Sub last;
} Last_Killer;

static void last_killer_cb(const void* item, void* user)
{
    Last_Killer* k = (Last_Killer*)user;
    MEL_UNUSED(item);
    mel_event_unsubscribe(k->ev, k->last);
}

MEL_TEST(event, sibling_unsubscribe_of_last_during_fire_no_uaf)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 8, mel_event_policy_latest(noop_overflow, nullptr));
    Mel_Executor* exec = mel_executor_inline();

    Last_Killer killer = { 0 };
    killer.ev = ev;

    Sink mid = { 0 };
    Sink last = { 0 };

    mel_event_subscribe_push(ev, exec, last_killer_cb, &killer);
    mel_event_subscribe_push(ev, exec, sink_cb, &mid);
    killer.last = mel_event_subscribe_push(ev, exec, sink_cb, &last);

    int v = 1;
    mel_event_fire(ev, &v);

    MEL_EXPECT_EQ(mid.count, 1);
    MEL_EXPECT_EQ(mel_event_subscriber_count(ev), 2u);

    mel_event_destroy(ev);
}

#define MEL_EVENT_STRESS_THREADS 4
#define MEL_EVENT_STRESS_PER     2000

typedef struct
{
    Mel_Event* ev;
    int        fired;
} Fire_Worker;

static int fire_worker(void* user)
{
    Fire_Worker* w = (Fire_Worker*)user;
    for (int i = 0; i < MEL_EVENT_STRESS_PER; i++)
    {
        mel_event_fire(w->ev, &i);
        w->fired++;
    }
    return 0;
}

typedef struct
{
    Mel_Event*   ev;
    _Atomic(i32) stop;
} Churn_Worker;

static int churn_worker(void* user)
{
    Churn_Worker* w = (Churn_Worker*)user;
    while (atomic_load_explicit(&w->stop, memory_order_acquire) == 0)
    {
        Mel_Event_Sub s = mel_event_subscribe_pull(w->ev, nullptr);
        int           out = 0;
        mel_event_pull(w->ev, s, &out);
        mel_event_pull_pending(w->ev, s);
        mel_event_lag(w->ev, s);
        mel_event_unsubscribe(w->ev, s);
    }
    return 0;
}

MEL_TEST(event, concurrent_fire_drains_every_item)
{
    u32 cap = MEL_EVENT_STRESS_THREADS * MEL_EVENT_STRESS_PER;

    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), cap, mel_event_policy_lossless(noop_overflow, nullptr));
    Mel_Event_Sub collector = mel_event_subscribe_pull(ev, nullptr);

    Churn_Worker churn = { 0 };
    churn.ev = ev;
    atomic_store(&churn.stop, 0);
    Mel_Thread churn_thread;
    MEL_REQUIRE(mel_thread_spawn(&churn_thread, churn_worker, &churn, .name = "churn"));

    Fire_Worker workers[MEL_EVENT_STRESS_THREADS] = { 0 };
    Mel_Thread  threads[MEL_EVENT_STRESS_THREADS];
    for (int i = 0; i < MEL_EVENT_STRESS_THREADS; i++)
    {
        workers[i].ev = ev;
        MEL_REQUIRE(mel_thread_spawn(&threads[i], fire_worker, &workers[i], .name = "fire"));
    }
    for (int i = 0; i < MEL_EVENT_STRESS_THREADS; i++)
        mel_thread_join(&threads[i], nullptr);

    atomic_store_explicit(&churn.stop, 1, memory_order_release);
    mel_thread_join(&churn_thread, nullptr);

    u32 drained = 0;
    int out = 0;
    while (mel_event_pull(ev, collector, &out))
        drained++;

    u64 lag = mel_event_lag(ev, collector);

    MEL_EXPECT_EQ((u64)drained + lag, (u64)cap);
    MEL_EXPECT_EQ(lag, (u64)0);
    MEL_EXPECT_EQ(drained, cap);

    mel_event_unsubscribe(ev, collector);
    mel_event_destroy(ev);
}

typedef struct
{
    _Atomic(i32) total;
} Atomic_Sink;

static void atomic_sink_cb(const void* item, void* user)
{
    Atomic_Sink* s = (Atomic_Sink*)user;
    MEL_UNUSED(item);
    atomic_fetch_add_explicit(&s->total, 1, memory_order_relaxed);
}

MEL_TEST(event, concurrent_fire_push_delivers_every_item)
{
    u32 cap = MEL_EVENT_STRESS_THREADS * MEL_EVENT_STRESS_PER;

    Mel_Event*  ev = mel_event_create(mel_alloc_heap(), sizeof(int), cap, mel_event_policy_lossless(noop_overflow, nullptr));
    Atomic_Sink sink = { 0 };
    atomic_store(&sink.total, 0);
    Mel_Event_Sub sub = mel_event_subscribe_push(ev, mel_executor_inline(), atomic_sink_cb, &sink);

    Fire_Worker workers[MEL_EVENT_STRESS_THREADS] = { 0 };
    Mel_Thread  threads[MEL_EVENT_STRESS_THREADS];
    for (int i = 0; i < MEL_EVENT_STRESS_THREADS; i++)
    {
        workers[i].ev = ev;
        MEL_REQUIRE(mel_thread_spawn(&threads[i], fire_worker, &workers[i], .name = "fire"));
    }
    for (int i = 0; i < MEL_EVENT_STRESS_THREADS; i++)
        mel_thread_join(&threads[i], nullptr);

    i32 delivered = atomic_load(&sink.total);
    u32 leftover = mel_event_pull_pending(ev, sub);

    MEL_EXPECT_EQ((u64)delivered + (u64)leftover + mel_event_lag(ev, sub), (u64)cap);
    MEL_EXPECT_EQ(mel_event_lag(ev, sub), (u64)0);
    MEL_EXPECT_EQ((u64)delivered, (u64)cap);
    MEL_EXPECT_EQ(leftover, 0u);

    mel_event_unsubscribe(ev, sub);
    mel_event_destroy(ev);
}

static void churn_push_cb(const void* item, void* user)
{
    _Atomic(i32)* seen = (_Atomic(i32)*)user;
    MEL_UNUSED(item);
    atomic_fetch_add_explicit(seen, 1, memory_order_relaxed);
}

typedef struct
{
    Mel_Event*   ev;
    _Atomic(i32) stop;
    _Atomic(i32) seen;
} Push_Churn;

static int push_churn_worker(void* user)
{
    Push_Churn* w = (Push_Churn*)user;
    while (atomic_load_explicit(&w->stop, memory_order_acquire) == 0)
    {
        Mel_Event_Sub s = mel_event_subscribe_push(w->ev, mel_executor_inline(), churn_push_cb, &w->seen);
        mel_event_unsubscribe(w->ev, s);
    }
    return 0;
}

#define MEL_EVENT_CHURN_THREADS 3

MEL_TEST(event, concurrent_fire_with_push_churn_no_race)
{
    Mel_Event*  ev = mel_event_create(mel_alloc_heap(), sizeof(int), 64, mel_event_policy_latest(noop_overflow, nullptr));
    Atomic_Sink sink = { 0 };
    atomic_store(&sink.total, 0);
    Mel_Event_Sub stable = mel_event_subscribe_push(ev, mel_executor_inline(), atomic_sink_cb, &sink);

    Push_Churn churn[MEL_EVENT_CHURN_THREADS] = { 0 };
    Mel_Thread churn_threads[MEL_EVENT_CHURN_THREADS];
    for (int i = 0; i < MEL_EVENT_CHURN_THREADS; i++)
    {
        churn[i].ev = ev;
        atomic_store(&churn[i].stop, 0);
        atomic_store(&churn[i].seen, 0);
        MEL_REQUIRE(mel_thread_spawn(&churn_threads[i], push_churn_worker, &churn[i], .name = "pchurn"));
    }

    Fire_Worker workers[MEL_EVENT_STRESS_THREADS] = { 0 };
    Mel_Thread  threads[MEL_EVENT_STRESS_THREADS];
    for (int i = 0; i < MEL_EVENT_STRESS_THREADS; i++)
    {
        workers[i].ev = ev;
        MEL_REQUIRE(mel_thread_spawn(&threads[i], fire_worker, &workers[i], .name = "fire"));
    }
    for (int i = 0; i < MEL_EVENT_STRESS_THREADS; i++)
        mel_thread_join(&threads[i], nullptr);

    for (int i = 0; i < MEL_EVENT_CHURN_THREADS; i++)
        atomic_store_explicit(&churn[i].stop, 1, memory_order_release);
    for (int i = 0; i < MEL_EVENT_CHURN_THREADS; i++)
        mel_thread_join(&churn_threads[i], nullptr);

    MEL_EXPECT_EQ(mel_event_subscriber_count(ev), 1u);

    mel_event_unsubscribe(ev, stable);
    mel_event_destroy(ev);
}

MEL_TEST(event, push_churn_under_fire_no_node_leak)
{
    atomic_store(&g_count_live, 0);
    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Event*  ev = mel_event_create(&counting, sizeof(int), 16, mel_event_policy_latest(noop_overflow, nullptr));
    Atomic_Sink sink = { 0 };
    atomic_store(&sink.total, 0);
    Mel_Event_Sub stable = mel_event_subscribe_push(ev, mel_executor_inline(), atomic_sink_cb, &sink);

    Push_Churn churn[MEL_EVENT_CHURN_THREADS] = { 0 };
    Mel_Thread churn_threads[MEL_EVENT_CHURN_THREADS];
    for (int i = 0; i < MEL_EVENT_CHURN_THREADS; i++)
    {
        churn[i].ev = ev;
        atomic_store(&churn[i].stop, 0);
        atomic_store(&churn[i].seen, 0);
        MEL_REQUIRE(mel_thread_spawn(&churn_threads[i], push_churn_worker, &churn[i], .name = "pchurn"));
    }

    Fire_Worker workers[MEL_EVENT_STRESS_THREADS] = { 0 };
    Mel_Thread  threads[MEL_EVENT_STRESS_THREADS];
    for (int i = 0; i < MEL_EVENT_STRESS_THREADS; i++)
    {
        workers[i].ev = ev;
        MEL_REQUIRE(mel_thread_spawn(&threads[i], fire_worker, &workers[i], .name = "fire"));
    }
    for (int i = 0; i < MEL_EVENT_STRESS_THREADS; i++)
        mel_thread_join(&threads[i], nullptr);

    for (int i = 0; i < MEL_EVENT_CHURN_THREADS; i++)
        atomic_store_explicit(&churn[i].stop, 1, memory_order_release);
    for (int i = 0; i < MEL_EVENT_CHURN_THREADS; i++)
        mel_thread_join(&churn_threads[i], nullptr);

    mel_event_unsubscribe(ev, stable);
    mel_event_destroy(ev);

    MEL_EXPECT_EQ((i64)atomic_load(&g_count_live), (i64)0);
}

typedef struct
{
    Mel_Event*    ev;
    Mel_Event_Sub self;
    Mel_Event_Sub sibling;
    _Atomic(i32)* live_subs;
} Reentrant_Ctx;

static void reentrant_cb(const void* item, void* user)
{
    Reentrant_Ctx* c = (Reentrant_Ctx*)user;
    int            v = *(const int*)item;
    if (v == 1)
    {
        Mel_Event_Sub extra = mel_event_subscribe_pull(c->ev, nullptr);
        mel_event_unsubscribe(c->ev, extra);
        int v2 = 2;
        mel_event_fire(c->ev, &v2);
    }
}

MEL_TEST(event, reentrant_callback_subscribe_fire_unsubscribe_no_deadlock)
{
    Mel_Event*    ev = mel_event_create(mel_alloc_heap(), sizeof(int), 8, mel_event_policy_latest(noop_overflow, nullptr));
    Mel_Executor* exec = mel_executor_inline();

    Reentrant_Ctx ctx = { 0 };
    ctx.ev = ev;
    ctx.self = mel_event_subscribe_push(ev, exec, reentrant_cb, &ctx);

    int v = 1;
    mel_event_fire(ev, &v);

    MEL_EXPECT_EQ(mel_event_subscriber_count(ev), 1u);

    mel_event_unsubscribe(ev, ctx.self);
    mel_event_destroy(ev);
}

typedef struct
{
    Mel_Executor   base;
    Mel_Mpsc_Node* head;
    Mel_Mpsc_Node* tail;
} Deferred_Executor;

static void deferred_submit(Mel_Executor* self, Mel_Task* task)
{
    Deferred_Executor* d = (Deferred_Executor*)self;
    i32                expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&task->armed, &expected, 1, memory_order_acq_rel, memory_order_acquire))
        return;
    atomic_store_explicit(&task->link.next, nullptr, memory_order_relaxed);
    if (d->tail != nullptr)
        atomic_store_explicit(&d->tail->next, &task->link, memory_order_relaxed);
    else
        d->head = &task->link;
    d->tail = &task->link;
}

static u32 deferred_drain(Deferred_Executor* d)
{
    u32 ran = 0;
    while (d->head != nullptr)
    {
        Mel_Mpsc_Node* node = d->head;
        Mel_Task*      task = mel_container_of(node, Mel_Task, link);
        d->head = atomic_load_explicit(&node->next, memory_order_relaxed);
        if (d->head == nullptr)
            d->tail = nullptr;
        atomic_store_explicit(&node->next, nullptr, memory_order_relaxed);
        atomic_store_explicit(&task->armed, 0, memory_order_release);
        task->run(task);
        ran++;
    }
    return ran;
}

MEL_TEST(event, destroy_with_deferred_delivery_in_flight_no_uaf)
{
    atomic_store(&g_count_live, 0);
    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Deferred_Executor d = { 0 };
    d.base.submit = deferred_submit;

    Mel_Event* ev = mel_event_create(&counting, sizeof(int), 8, mel_event_policy_lossless(noop_overflow, nullptr));

    Sink          s = { 0 };
    Mel_Event_Sub sub = mel_event_subscribe_push(ev, &d.base, sink_cb, &s);
    Mel_Event_Sub pull_sub = mel_event_subscribe_pull(ev, nullptr);
    MEL_UNUSED(sub);
    MEL_UNUSED(pull_sub);

    int v = 0x1234;
    mel_event_fire(ev, &v);

    MEL_EXPECT_EQ(s.count, 0);

    mel_event_destroy(ev);

    MEL_EXPECT_GT((i64)atomic_load(&g_count_live), (i64)0);

    u32 ran = deferred_drain(&d);

    MEL_EXPECT_EQ(ran, 1u);
    MEL_EXPECT_EQ(s.count, 1);
    MEL_EXPECT_EQ(s.received[0], 0x1234);

    MEL_EXPECT_EQ((i64)atomic_load(&g_count_live), (i64)0);
}

MEL_TEST(event, destroy_with_deferred_delivery_multi_subscriber)
{
    Deferred_Executor d = { 0 };
    d.base.submit = deferred_submit;

    Mel_Event* ev = mel_event_create(mel_alloc_heap(), sizeof(int), 8, mel_event_policy_lossless(noop_overflow, nullptr));

    Sink a = { 0 }, b = { 0 };
    mel_event_subscribe_push(ev, &d.base, sink_cb, &a);
    mel_event_subscribe_push(ev, &d.base, sink_cb, &b);
    Mel_Event_Sub pull_sub = mel_event_subscribe_pull(ev, nullptr);
    MEL_UNUSED(pull_sub);

    int v1 = 11, v2 = 22;
    mel_event_fire(ev, &v1);
    mel_event_fire(ev, &v2);

    MEL_EXPECT_EQ(a.count, 0);
    MEL_EXPECT_EQ(b.count, 0);

    mel_event_destroy(ev);

    deferred_drain(&d);

    MEL_EXPECT_EQ(a.count, 2);
    MEL_EXPECT_EQ(a.received[0], 11);
    MEL_EXPECT_EQ(a.received[1], 22);
    MEL_EXPECT_EQ(b.count, 2);
    MEL_EXPECT_EQ(b.received[0], 11);
    MEL_EXPECT_EQ(b.received[1], 22);
}
