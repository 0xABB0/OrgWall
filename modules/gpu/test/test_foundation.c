#include <test/test.h>

#include <gpu/handle.h>
#include <gpu/status.h>
#include <gpu/future.h>
#include <gpu/device.h>
#include <gpu/threading.h>

#include <future/future.h>
#include <collection/slotmap.h>
#include <allocator/heap.h>
#include <thread/thread.h>

#include <stdatomic.h>

MEL_GPU_HANDLE(Test_Handle);

typedef struct
{
    Mel_Gpu_Resource_Header header;
    u32                     payload;
} Test_Resource;

MEL_TEST(handle, lifecycle_and_generation)
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(Test_Resource), .initial_capacity = 4);

    Test_Resource r = { .header = { .ownership = MEL_GPU_OWNERSHIP_OWNED }, .payload = 0xABCD };
    Test_Handle   h = { mel_slotmap_insert(&sm, &r) };

    MEL_EXPECT(mel_slotmap_alive(&sm, h.slot));
    Test_Resource* got = mel_slotmap_get(&sm, h.slot);
    MEL_REQUIRE_NOT_NULL(got);
    MEL_EXPECT_EQ(got->payload, 0xABCDu);
    MEL_EXPECT_EQ(got->header.ownership, MEL_GPU_OWNERSHIP_OWNED);

    Test_Handle stale = h;
    MEL_EXPECT(mel_slotmap_remove(&sm, h.slot));
    MEL_EXPECT(!mel_slotmap_alive(&sm, stale.slot));
    MEL_EXPECT_NULL(mel_slotmap_get(&sm, stale.slot));

    Test_Resource r2 = { .payload = 0x1234 };
    Test_Handle   h2 = { mel_slotmap_insert(&sm, &r2) };
    MEL_EXPECT_EQ(h2.slot.index, stale.slot.index);
    MEL_EXPECT_NEQ(h2.slot.generation, stale.slot.generation);
    MEL_EXPECT(!mel_slotmap_alive(&sm, stale.slot));
    MEL_EXPECT(mel_slotmap_alive(&sm, h2.slot));

    mel_slotmap_free(&sm);
}

MEL_TEST(handle, packed_swap_remove_keeps_others)
{
    Mel_SlotMap sm;
    mel_slotmap_init(&sm, mel_alloc_heap(), .item_size = sizeof(Test_Resource), .initial_capacity = 4);

    Test_Resource a = { .payload = 1 };
    Test_Resource b = { .payload = 2 };
    Test_Resource c = { .payload = 3 };
    Test_Handle   ha = { mel_slotmap_insert(&sm, &a) };
    Test_Handle   hb = { mel_slotmap_insert(&sm, &b) };
    Test_Handle   hc = { mel_slotmap_insert(&sm, &c) };

    MEL_EXPECT(mel_slotmap_remove(&sm, hb.slot));

    Test_Resource* ga = mel_slotmap_get(&sm, ha.slot);
    Test_Resource* gc = mel_slotmap_get(&sm, hc.slot);
    MEL_REQUIRE_NOT_NULL(ga);
    MEL_REQUIRE_NOT_NULL(gc);
    MEL_EXPECT_EQ(ga->payload, 1u);
    MEL_EXPECT_EQ(gc->payload, 3u);
    MEL_EXPECT_NULL(mel_slotmap_get(&sm, hb.slot));
    MEL_EXPECT_EQ(mel_slotmap_count(&sm), 2u);

    mel_slotmap_free(&sm);
}

enum
{
    TEST_STATUS_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    TEST_STATUS_SUBSTITUTE = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_WARNED),
    TEST_STATUS_NO_ADAPTER = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
};

MEL_TEST(status, severity_bit_tests)
{
    MEL_EXPECT(mel_gpu_ok(TEST_STATUS_OK));
    MEL_EXPECT(!mel_gpu_failed(TEST_STATUS_OK));
    MEL_EXPECT(!mel_gpu_warned(TEST_STATUS_OK));

    MEL_EXPECT(mel_gpu_warned(TEST_STATUS_SUBSTITUTE));
    MEL_EXPECT(!mel_gpu_failed(TEST_STATUS_SUBSTITUTE));
    MEL_EXPECT_EQ(mel_gpu_severity(TEST_STATUS_SUBSTITUTE), MEL_GPU_SEVERITY_WARNED);

    MEL_EXPECT(mel_gpu_failed(TEST_STATUS_NO_ADAPTER));
    MEL_EXPECT_EQ(mel_gpu_severity(TEST_STATUS_NO_ADAPTER), MEL_GPU_SEVERITY_ERROR);
    MEL_EXPECT_EQ((u32)(TEST_STATUS_NO_ADAPTER >> 2), 2u);
}

static void test_set_flag_cont(Mel_Gpu_Future* f, void* user)
{
    (void)f;
    *(int*)user += 1;
}

MEL_TEST(future, manual_resolve_and_deliver)
{
    Mel_Gpu_Completion_Pump* pump = mel_gpu_pump_create(NULL);
    Mel_Gpu_Future*          f = mel_gpu_future_create(pump, NULL);

    int delivered = 0;
    mel_gpu_future_then(f, test_set_flag_cont, &delivered);

    MEL_EXPECT(!mel_gpu_future_resolved(f));
    MEL_EXPECT_EQ(delivered, 0);

    int sentinel = 77;
    mel_gpu_future_resolve(f, &sentinel, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));

    MEL_EXPECT(mel_gpu_future_resolved(f));
    MEL_EXPECT_EQ(mel_gpu_future_value(f), &sentinel);
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    MEL_EXPECT_EQ(delivered, 1);

    mel_gpu_future_destroy(f);
    mel_gpu_pump_destroy(pump);
}

MEL_TEST(future, double_resolve_is_idempotent)
{
    Mel_Gpu_Completion_Pump* pump = mel_gpu_pump_create(NULL);
    Mel_Gpu_Future*          f = mel_gpu_future_create(pump, NULL);

    int delivered = 0;
    mel_gpu_future_then(f, test_set_flag_cont, &delivered);

    int first = 1, second = 2;
    mel_gpu_future_resolve(f, &first, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));
    mel_gpu_future_resolve(f, &second, MEL_GPU_STATUS(9, MEL_GPU_SEVERITY_ERROR));

    MEL_EXPECT_EQ(mel_gpu_future_value(f), &first);
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    MEL_EXPECT_EQ(delivered, 1);

    mel_gpu_future_destroy(f);
    mel_gpu_pump_destroy(pump);
}

typedef struct
{
    Mel_Gpu_Future* f;
    _Atomic(i32)    ready;
} Poll_Ctx;

static bool test_poller(void* user)
{
    Poll_Ctx* c = user;
    if (atomic_load(&c->ready) && !mel_gpu_future_resolved(c->f))
        mel_gpu_future_resolve(c->f, NULL, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));
    return true;
}

MEL_TEST(future, poller_resolves_on_tick)
{
    Mel_Gpu_Completion_Pump* pump = mel_gpu_pump_create(NULL);
    Mel_Gpu_Future*          f = mel_gpu_future_create(pump, NULL);

    int      delivered = 0;
    Poll_Ctx ctx = { .f = f };
    atomic_store(&ctx.ready, 0);
    mel_gpu_future_then(f, test_set_flag_cont, &delivered);
    mel_gpu_pump_add_poller(pump, test_poller, &ctx);

    mel_gpu_pump_tick(pump);
    MEL_EXPECT(!mel_gpu_future_resolved(f));

    atomic_store(&ctx.ready, 1);
    mel_gpu_pump_tick(pump);
    MEL_EXPECT(mel_gpu_future_resolved(f));
    MEL_EXPECT_EQ(delivered, 1);

    mel_gpu_pump_remove_poller(pump, test_poller, &ctx);
    mel_gpu_future_destroy(f);
    mel_gpu_pump_destroy(pump);
}

static int test_resolver_thread(void* user)
{
    Mel_Gpu_Future* f = user;
    mel_thread_sleep(2000000);
    mel_gpu_future_resolve(f, NULL, MEL_GPU_STATUS(5, MEL_GPU_SEVERITY_OK));
    return 0;
}

MEL_TEST(future, cross_thread_wait)
{
    Mel_Gpu_Completion_Pump* pump = mel_gpu_pump_create(NULL);
    Mel_Gpu_Future*          f = mel_gpu_future_create(pump, NULL);

    Mel_Thread th;
    MEL_REQUIRE(mel_thread_spawn(&th, test_resolver_thread, f));

    u32 status = mel_gpu_future_wait(f);
    MEL_EXPECT(mel_gpu_ok(status));
    MEL_EXPECT_EQ((u32)(status >> 2), 5u);

    mel_thread_join(&th, NULL);
    mel_gpu_future_destroy(f);
    mel_gpu_pump_destroy(pump);
}

typedef struct
{
    int   count;
    void* value;
    u32   status;
} Cont_Record;

static void test_record_cont(Mel_Gpu_Future* f, void* user)
{
    Cont_Record* r = user;
    r->count += 1;
    r->value = mel_gpu_future_value(f);
    r->status = mel_gpu_future_status(f);
}

MEL_TEST(future, then_after_resolve_delivers_once)
{
    Mel_Gpu_Future* f = mel_gpu_future_create(NULL, NULL);
    int             sentinel = 17;
    mel_gpu_future_resolve(f, &sentinel, MEL_GPU_STATUS(4, MEL_GPU_SEVERITY_OK));

    Cont_Record rec = { 0 };
    mel_gpu_future_then(f, test_record_cont, &rec);

    MEL_EXPECT_EQ(rec.count, 1);
    MEL_EXPECT_EQ(rec.value, &sentinel);
    MEL_EXPECT_EQ((u32)(rec.status >> 2), 4u);

    mel_gpu_future_destroy(f);
}

MEL_TEST(future, bit2_status_code_roundtrips_without_cancel)
{
    MEL_EXPECT_NEQ((u32)(MEL_GPU_DEVICE_CREATE_DEGRADED & MEL_FUTURE_CANCELLED), 0u);

    Mel_Gpu_Future* f = mel_gpu_future_create(NULL, NULL);
    Cont_Record     rec = { 0 };
    mel_gpu_future_then(f, test_record_cont, &rec);
    mel_gpu_future_resolve(f, NULL, MEL_GPU_DEVICE_CREATE_DEGRADED);

    MEL_EXPECT_EQ(rec.count, 1);
    MEL_EXPECT_EQ(mel_gpu_future_status(f), (u32)MEL_GPU_DEVICE_CREATE_DEGRADED);
    MEL_EXPECT(mel_gpu_warned(mel_gpu_future_status(f)));

    Mel_Future_Status base = mel_future_status(mel_gpu_future_shared(f));
    MEL_EXPECT_EQ((u32)(base & MEL_FUTURE_CANCELLED), 0u);
    MEL_EXPECT_EQ((u32)(base & MEL_FUTURE_SEVERITY_MASK), (u32)MEL_FUTURE_WARNED);

    mel_gpu_future_destroy(f);
}

MEL_TEST(future, error_status_preserves_value)
{
    Mel_Gpu_Future* f = mel_gpu_future_create(NULL, NULL);
    int             payload = 5;
    mel_gpu_future_resolve(f, &payload, MEL_GPU_DEVICE_CREATE_OOM);

    MEL_EXPECT(mel_gpu_failed(mel_gpu_future_status(f)));
    MEL_EXPECT_EQ(mel_gpu_future_value(f), &payload);
    MEL_EXPECT_EQ(mel_gpu_future_status(f), (u32)MEL_GPU_DEVICE_CREATE_OOM);

    mel_gpu_future_destroy(f);
}

MEL_TEST(future, shared_composes_with_when_all)
{
    Mel_Gpu_Future*  fa = mel_gpu_future_create(NULL, NULL);
    Mel_Gpu_Future*  fb = mel_gpu_future_create(NULL, NULL);
    Mel_Future*      inputs[2] = { mel_gpu_future_shared(fa), mel_gpu_future_shared(fb) };
    Mel_Future_When* w = mel_future_when_all(inputs, 2, mel_alloc_heap());
    Mel_Future*      result = mel_future_when_future(w);

    MEL_EXPECT(!mel_future_resolved(result));
    mel_gpu_future_resolve(fa, NULL, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));
    MEL_EXPECT(!mel_future_resolved(result));
    mel_gpu_future_resolve(fb, NULL, MEL_GPU_DEVICE_CREATE_DEGRADED);
    MEL_EXPECT(mel_future_resolved(result));
    MEL_EXPECT_EQ((u32)(mel_future_status(result) & MEL_FUTURE_SEVERITY_MASK), (u32)MEL_FUTURE_WARNED);

    mel_future_when_free(w);
    mel_gpu_future_destroy(fa);
    mel_gpu_future_destroy(fb);
}

typedef struct
{
    Mel_Gpu_Future* f;
    _Atomic(i32)*   start;
    u32             status;
} Race_Resolve_Arg;

static int test_race_resolve_thread(void* user)
{
    Race_Resolve_Arg* a = user;
    while (atomic_load(a->start) == 0)
        mel_thread_sleep(1000);
    mel_gpu_future_resolve(a->f, NULL, a->status);
    return 0;
}

MEL_TEST(future, concurrent_resolve_claims_status_once)
{
    for (int rep = 0; rep < 64; rep++)
    {
        Mel_Gpu_Future* f = mel_gpu_future_create(NULL, NULL);
        Cont_Record     rec = { 0 };
        mel_gpu_future_then(f, test_record_cont, &rec);

        _Atomic(i32) start;
        atomic_store(&start, 0);
        Race_Resolve_Arg a0 = { f, &start, MEL_GPU_STATUS(100, MEL_GPU_SEVERITY_OK) };
        Race_Resolve_Arg a1 = { f, &start, MEL_GPU_STATUS(200, MEL_GPU_SEVERITY_ERROR) };

        Mel_Thread t0, t1;
        MEL_REQUIRE(mel_thread_spawn(&t0, test_race_resolve_thread, &a0));
        MEL_REQUIRE(mel_thread_spawn(&t1, test_race_resolve_thread, &a1));
        atomic_store(&start, 1);
        mel_thread_join(&t0, NULL);
        mel_thread_join(&t1, NULL);

        MEL_EXPECT_EQ(rec.count, 1);
        MEL_EXPECT(mel_gpu_future_resolved(f));
        u32 s = mel_gpu_future_status(f);
        u32 code = s >> 2;
        MEL_EXPECT(code == 100u || code == 200u);
        MEL_EXPECT_EQ(rec.status, s);
        mel_gpu_future_destroy(f);
    }
}

MEL_TEST(threading, tracker_same_thread_reentry)
{
    Mel_Gpu_Thread_Tracker* t = mel_gpu_thread_tracker_create();
    int                     object = 0;

    mel_gpu_thread_tracker_enter(t, &object, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    mel_gpu_thread_tracker_enter(t, &object, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    mel_gpu_thread_tracker_exit(t, &object);
    mel_gpu_thread_tracker_exit(t, &object);

    mel_gpu_thread_tracker_destroy(t);
}
