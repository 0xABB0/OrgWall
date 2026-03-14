#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.wsq.h"
#include <pthread.h>
#include <stdatomic.h>

MEL_TEST(wsq_single_thread_lifo, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Wsq q = mel_wsq_create(alloc, .initial_capacity = 8);

    for (i64 i = 1; i <= 100; i++)
        mel_wsq_push(&q, (void*)(uintptr_t)i);

    for (i64 i = 100; i >= 1; i--)
    {
        void* v = mel_wsq_pop(&q);
        MEL_ASSERT_NEQ(v, MEL_WSQ_EMPTY);
        MEL_ASSERT_EQ((i64)(uintptr_t)v, i);
    }

    MEL_ASSERT_EQ(mel_wsq_pop(&q), MEL_WSQ_EMPTY);

    mel_wsq_destroy(&q);
}

MEL_TEST(wsq_push_beyond_capacity, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Wsq q = mel_wsq_create(alloc, .initial_capacity = 4);

    for (i64 i = 1; i <= 1000; i++)
        mel_wsq_push(&q, (void*)(uintptr_t)i);

    MEL_ASSERT_EQ(mel_wsq_size(&q), 1000);

    for (i64 i = 1000; i >= 1; i--)
    {
        void* v = mel_wsq_pop(&q);
        MEL_ASSERT_NEQ(v, MEL_WSQ_EMPTY);
        MEL_ASSERT_EQ((i64)(uintptr_t)v, i);
    }

    MEL_ASSERT_EQ(mel_wsq_pop(&q), MEL_WSQ_EMPTY);

    mel_wsq_destroy(&q);
}

MEL_TEST(wsq_empty_pop_and_steal, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Wsq q = mel_wsq_create(alloc);

    MEL_ASSERT_EQ(mel_wsq_pop(&q), MEL_WSQ_EMPTY);
    MEL_ASSERT_EQ(mel_wsq_steal(&q), MEL_WSQ_EMPTY);

    mel_wsq_push(&q, (void*)(uintptr_t)42);
    void* v = mel_wsq_pop(&q);
    MEL_ASSERT_EQ((i64)(uintptr_t)v, 42);

    MEL_ASSERT_EQ(mel_wsq_pop(&q), MEL_WSQ_EMPTY);
    MEL_ASSERT_EQ(mel_wsq_steal(&q), MEL_WSQ_EMPTY);

    mel_wsq_destroy(&q);
}

#define CONCURRENT_COUNT 100000
#define STEALER_COUNT 4

typedef struct {
    Mel_Wsq* q;
    _Atomic(i64) stolen_sum;
    _Atomic(i64) stolen_count;
} Stealer_Ctx;

static void* stealer_thread(void* arg)
{
    Stealer_Ctx* ctx = arg;
    i64 local_sum = 0;
    i64 local_count = 0;

    for (;;)
    {
        void* v = mel_wsq_steal(ctx->q);
        if (v == MEL_WSQ_EMPTY || v == MEL_WSQ_ABORT)
        {
            if (atomic_load_explicit(&ctx->q->bottom, memory_order_acquire) <=
                atomic_load_explicit(&ctx->q->top, memory_order_acquire))
            {
                void* v2 = mel_wsq_steal(ctx->q);
                if (v2 == MEL_WSQ_EMPTY || v2 == MEL_WSQ_ABORT)
                    break;
                local_sum += (i64)(uintptr_t)v2;
                local_count++;
            }
            continue;
        }
        local_sum += (i64)(uintptr_t)v;
        local_count++;
    }

    atomic_fetch_add_explicit(&ctx->stolen_sum, local_sum, memory_order_relaxed);
    atomic_fetch_add_explicit(&ctx->stolen_count, local_count, memory_order_relaxed);
    return NULL;
}

MEL_TEST(wsq_1_producer_4_stealers, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Wsq q = mel_wsq_create(alloc, .initial_capacity = 256);

    for (i64 i = 1; i <= CONCURRENT_COUNT; i++)
        mel_wsq_push(&q, (void*)(uintptr_t)i);

    Stealer_Ctx ctx = {
        .q = &q,
    };
    atomic_store(&ctx.stolen_sum, 0);
    atomic_store(&ctx.stolen_count, 0);

    pthread_t threads[STEALER_COUNT];
    for (i32 i = 0; i < STEALER_COUNT; i++)
        pthread_create(&threads[i], NULL, stealer_thread, &ctx);

    i64 popped_sum = 0;
    i64 popped_count = 0;
    for (;;)
    {
        void* v = mel_wsq_pop(&q);
        if (v == MEL_WSQ_EMPTY) break;
        popped_sum += (i64)(uintptr_t)v;
        popped_count++;
    }

    for (i32 i = 0; i < STEALER_COUNT; i++)
        pthread_join(threads[i], NULL);

    i64 total_count = popped_count + atomic_load(&ctx.stolen_count);
    i64 total_sum = popped_sum + atomic_load(&ctx.stolen_sum);
    i64 expected_sum = (i64)CONCURRENT_COUNT * (i64)(CONCURRENT_COUNT + 1) / 2;

    MEL_ASSERT_EQ(total_count, (i64)CONCURRENT_COUNT);
    MEL_ASSERT_EQ(total_sum, expected_sum);

    mel_wsq_destroy(&q);
}

typedef struct {
    Mel_Wsq* q;
    _Atomic(i64) stolen_sum;
    _Atomic(i64) stolen_count;
    _Atomic(bool) producer_done;
} Interleave_Ctx;

static void* interleave_stealer_thread(void* arg)
{
    Interleave_Ctx* ctx = arg;
    i64 local_sum = 0;
    i64 local_count = 0;

    for (;;)
    {
        void* v = mel_wsq_steal(ctx->q);
        if (v != MEL_WSQ_EMPTY && v != MEL_WSQ_ABORT)
        {
            local_sum += (i64)(uintptr_t)v;
            local_count++;
            continue;
        }
        if (atomic_load_explicit(&ctx->producer_done, memory_order_acquire))
        {
            i32 retries = 0;
            while (retries < 256)
            {
                v = mel_wsq_steal(ctx->q);
                if (v != MEL_WSQ_EMPTY && v != MEL_WSQ_ABORT)
                {
                    local_sum += (i64)(uintptr_t)v;
                    local_count++;
                    retries = 0;
                    continue;
                }
                retries++;
            }
            break;
        }
    }

    atomic_fetch_add_explicit(&ctx->stolen_sum, local_sum, memory_order_relaxed);
    atomic_fetch_add_explicit(&ctx->stolen_count, local_count, memory_order_relaxed);
    return NULL;
}

MEL_TEST(wsq_interleaved_push_pop_steal, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Wsq q = mel_wsq_create(alloc, .initial_capacity = 64);

    Interleave_Ctx ctx = { .q = &q };
    atomic_store(&ctx.stolen_sum, 0);
    atomic_store(&ctx.stolen_count, 0);
    atomic_store(&ctx.producer_done, false);

    pthread_t threads[STEALER_COUNT];
    for (i32 i = 0; i < STEALER_COUNT; i++)
        pthread_create(&threads[i], NULL, interleave_stealer_thread, &ctx);

    i64 popped_sum = 0;
    i64 popped_count = 0;

    for (i64 i = 1; i <= CONCURRENT_COUNT; i++)
    {
        mel_wsq_push(&q, (void*)(uintptr_t)i);

        if (i % 3 == 0)
        {
            void* v = mel_wsq_pop(&q);
            if (v != MEL_WSQ_EMPTY)
            {
                popped_sum += (i64)(uintptr_t)v;
                popped_count++;
            }
        }
    }

    for (;;)
    {
        void* v = mel_wsq_pop(&q);
        if (v == MEL_WSQ_EMPTY) break;
        popped_sum += (i64)(uintptr_t)v;
        popped_count++;
    }

    atomic_store_explicit(&ctx.producer_done, true, memory_order_release);

    for (i32 i = 0; i < STEALER_COUNT; i++)
        pthread_join(threads[i], NULL);

    i64 total_count = popped_count + atomic_load(&ctx.stolen_count);
    i64 total_sum = popped_sum + atomic_load(&ctx.stolen_sum);
    i64 expected_sum = (i64)CONCURRENT_COUNT * (i64)(CONCURRENT_COUNT + 1) / 2;

    MEL_ASSERT_EQ(total_count, (i64)CONCURRENT_COUNT);
    MEL_ASSERT_EQ(total_sum, expected_sum);

    mel_wsq_destroy(&q);
}

typedef struct {
    Mel_Wsq* q;
    _Atomic(i64) stolen_sum;
    _Atomic(i64) stolen_count;
    _Atomic(bool) done;
} Aba_Ctx;

static void* aba_stealer_thread(void* arg)
{
    Aba_Ctx* ctx = arg;
    i64 local_sum = 0;
    i64 local_count = 0;

    for (;;)
    {
        void* v = mel_wsq_steal(ctx->q);
        if (v != MEL_WSQ_EMPTY && v != MEL_WSQ_ABORT)
        {
            local_sum += (i64)(uintptr_t)v;
            local_count++;
            continue;
        }
        if (atomic_load_explicit(&ctx->done, memory_order_acquire))
        {
            i32 retries = 0;
            while (retries < 256)
            {
                v = mel_wsq_steal(ctx->q);
                if (v != MEL_WSQ_EMPTY && v != MEL_WSQ_ABORT)
                {
                    local_sum += (i64)(uintptr_t)v;
                    local_count++;
                    retries = 0;
                    continue;
                }
                retries++;
            }
            break;
        }
    }

    atomic_fetch_add_explicit(&ctx->stolen_sum, local_sum, memory_order_relaxed);
    atomic_fetch_add_explicit(&ctx->stolen_count, local_count, memory_order_relaxed);
    return NULL;
}

MEL_TEST(wsq_aba_stress, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Wsq q = mel_wsq_create(alloc, .initial_capacity = 8);

    Aba_Ctx ctx = { .q = &q };
    atomic_store(&ctx.stolen_sum, 0);
    atomic_store(&ctx.stolen_count, 0);
    atomic_store(&ctx.done, false);

    pthread_t threads[STEALER_COUNT];
    for (i32 i = 0; i < STEALER_COUNT; i++)
        pthread_create(&threads[i], NULL, aba_stealer_thread, &ctx);

    i64 popped_sum = 0;
    i64 popped_count = 0;
    i64 expected_sum = 0;
    i64 total_pushed = 0;
    i64 val = 1;

    for (i64 round = 0; round < 50000; round++)
    {
        mel_wsq_push(&q, (void*)(uintptr_t)val);
        expected_sum += val;
        total_pushed++;
        val++;

        void* v = mel_wsq_pop(&q);
        if (v != MEL_WSQ_EMPTY)
        {
            popped_sum += (i64)(uintptr_t)v;
            popped_count++;
        }

        mel_wsq_push(&q, (void*)(uintptr_t)val);
        expected_sum += val;
        total_pushed++;
        val++;
    }

    for (;;)
    {
        void* v = mel_wsq_pop(&q);
        if (v == MEL_WSQ_EMPTY) break;
        popped_sum += (i64)(uintptr_t)v;
        popped_count++;
    }

    atomic_store_explicit(&ctx.done, true, memory_order_release);

    for (i32 i = 0; i < STEALER_COUNT; i++)
        pthread_join(threads[i], NULL);

    i64 total_count = popped_count + atomic_load(&ctx.stolen_count);
    i64 total_sum = popped_sum + atomic_load(&ctx.stolen_sum);

    MEL_ASSERT_EQ(total_count, total_pushed);
    MEL_ASSERT_EQ(total_sum, expected_sum);

    mel_wsq_destroy(&q);
}
