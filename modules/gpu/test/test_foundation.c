#include <test/test.h>

#include <gpu/handle.h>
#include <gpu/status.h>
#include <gpu/future.h>
#include <gpu/threading.h>

#include <collection.slotmap/slotmap.h>
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

    int sentinel = 77;
    mel_gpu_future_resolve(f, &sentinel, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));

    MEL_EXPECT(mel_gpu_future_resolved(f));
    MEL_EXPECT_EQ(mel_gpu_future_value(f), &sentinel);
    MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
    MEL_EXPECT_EQ(delivered, 0);

    mel_gpu_pump_tick(pump);
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

    mel_gpu_pump_tick(pump);
    mel_gpu_pump_tick(pump);
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
