#include <execution/execution.h>
#include <test/test.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <stdatomic.h>

static int g_ran;

static void* add_then_return(void* ctx)
{
    g_ran++;
    int* p = (int*)ctx;
    *p += 41;
    return ctx;
}

MEL_TEST(execution, sync_wait_runs_work_and_returns_value)
{
    const Mel_Alloc* heap = mel_alloc_heap();
    g_ran = 0;
    int value = 1;

    Mel_Execution_Sender* sender = mel_execution_sender_create(heap, add_then_return, &value);
    MEL_REQUIRE_NOT_NULL(sender);

    void* result = mel_execution_sync_wait(sender);

    MEL_EXPECT_EQ(g_ran, 1);
    MEL_EXPECT_EQ(value, 42);
    MEL_EXPECT_EQ(result, (void*)&value);

    mel_execution_sender_destroy(heap, sender);
}

static _Atomic(i64) g_live;

static void* counting_cb(void* ptr, usize size, u32 align, const char* file, const char* func, u32 line, void* user_data)
{
    const Mel_Alloc* heap = mel_alloc_heap();

    if (ptr == NULL)
    {
        void* p = heap->alloc_cb(NULL, size, align, file, func, line, heap->user_data);
        if (p)
            atomic_fetch_add_explicit(&g_live, 1, memory_order_relaxed);
        return p;
    }

    if (size == 0)
    {
        heap->alloc_cb(ptr, 0, align, file, func, line, heap->user_data);
        atomic_fetch_sub_explicit(&g_live, 1, memory_order_relaxed);
        return NULL;
    }

    return heap->alloc_cb(ptr, size, align, file, func, line, heap->user_data);
}

static void* noop_work(void* ctx) { return ctx; }

MEL_TEST(execution, handle_storage_uses_caller_allocator_no_leak)
{
    atomic_store(&g_live, 0);
    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    int                   value  = 0;
    Mel_Execution_Sender* sender = mel_execution_sender_create(&counting, noop_work, &value);
    MEL_REQUIRE_NOT_NULL(sender);
    MEL_EXPECT_GT((i64)atomic_load(&g_live), (i64)0);

    (void)mel_execution_sync_wait(sender);
    mel_execution_sender_destroy(&counting, sender);

    MEL_EXPECT_EQ((i64)atomic_load(&g_live), (i64)0);
}
