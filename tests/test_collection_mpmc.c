#include "../melody/test.harness.h"
#include "../melody/collection.mpmc.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"

#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_atomic.h>
#include <string.h>

MEL_TEST(mpmc_single_thread_fifo, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Mpmc q;
    mel_mpmc_init(&q, 64, alloc);

    for (usize i = 1; i <= 32; i++)
        MEL_ASSERT(mel_mpmc_push(&q, (void*)i));

    for (usize i = 1; i <= 32; i++)
    {
        void* val = NULL;
        MEL_ASSERT(mel_mpmc_pop(&q, &val));
        MEL_ASSERT_EQ((usize)val, i);
    }

    mel_mpmc_free(&q);
}

MEL_TEST(mpmc_push_to_full, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Mpmc q;
    mel_mpmc_init(&q, 4, alloc);

    MEL_ASSERT(mel_mpmc_push(&q, (void*)1));
    MEL_ASSERT(mel_mpmc_push(&q, (void*)2));
    MEL_ASSERT(mel_mpmc_push(&q, (void*)3));
    MEL_ASSERT(mel_mpmc_push(&q, (void*)4));
    MEL_ASSERT(!mel_mpmc_push(&q, (void*)5));

    void* val = NULL;
    MEL_ASSERT(mel_mpmc_pop(&q, &val));
    MEL_ASSERT_EQ((usize)val, (usize)1);
    MEL_ASSERT(mel_mpmc_pop(&q, &val));
    MEL_ASSERT_EQ((usize)val, (usize)2);
    MEL_ASSERT(mel_mpmc_pop(&q, &val));
    MEL_ASSERT_EQ((usize)val, (usize)3);
    MEL_ASSERT(mel_mpmc_pop(&q, &val));
    MEL_ASSERT_EQ((usize)val, (usize)4);

    mel_mpmc_free(&q);
}

MEL_TEST(mpmc_pop_from_empty, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Mpmc q;
    mel_mpmc_init(&q, 4, alloc);

    void* val = NULL;
    MEL_ASSERT(!mel_mpmc_pop(&q, &val));

    MEL_ASSERT(mel_mpmc_push(&q, (void*)42));
    MEL_ASSERT(mel_mpmc_pop(&q, &val));
    MEL_ASSERT_EQ((usize)val, (usize)42);
    MEL_ASSERT(!mel_mpmc_pop(&q, &val));

    mel_mpmc_free(&q);
}

#define MPMC_ITEMS_PER_PRODUCER 10000
#define MPMC_PRODUCER_COUNT 4
#define MPMC_CONSUMER_COUNT 4
#define MPMC_TOTAL_MP (MPMC_ITEMS_PER_PRODUCER * MPMC_PRODUCER_COUNT)
#define MPMC_TOTAL_FULL 100000

typedef struct {
    Mel_Mpmc* q;
    i32 producer_id;
    i32 items_per_producer;
} Mel__Mpmc_Producer_Ctx;

typedef struct {
    Mel_Mpmc* q;
    i32 total_items;
    SDL_AtomicInt* consumed_count;
    u8* seen;
    SDL_SpinLock* seen_lock;
} Mel__Mpmc_Consumer_Ctx;

static i32 mel__mpmc_producer_fn(void* user)
{
    Mel__Mpmc_Producer_Ctx* ctx = (Mel__Mpmc_Producer_Ctx*)user;
    i32 base = ctx->producer_id * ctx->items_per_producer;
    for (i32 i = 0; i < ctx->items_per_producer; i++)
    {
        usize val = (usize)(base + i + 1);
        while (!mel_mpmc_push(ctx->q, (void*)val))
            ;
    }
    return 0;
}

static i32 mel__mpmc_consumer_fn(void* user)
{
    Mel__Mpmc_Consumer_Ctx* ctx = (Mel__Mpmc_Consumer_Ctx*)user;
    while (SDL_GetAtomicInt(ctx->consumed_count) < ctx->total_items)
    {
        void* val = NULL;
        if (mel_mpmc_pop(ctx->q, &val))
        {
            usize idx = (usize)val - 1;
            SDL_LockSpinlock(ctx->seen_lock);
            ctx->seen[idx]++;
            SDL_UnlockSpinlock(ctx->seen_lock);
            SDL_AddAtomicInt(ctx->consumed_count, 1);
        }
    }
    return 0;
}

MEL_TEST(mpmc_multi_producer_single_consumer, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Mpmc q;
    mel_mpmc_init(&q, 1024, alloc);

    Mel__Mpmc_Producer_Ctx prod_ctx[MPMC_PRODUCER_COUNT];
    SDL_Thread* producers[MPMC_PRODUCER_COUNT];

    for (i32 i = 0; i < MPMC_PRODUCER_COUNT; i++)
    {
        prod_ctx[i] = (Mel__Mpmc_Producer_Ctx){ .q = &q, .producer_id = i, .items_per_producer = MPMC_ITEMS_PER_PRODUCER };
        producers[i] = SDL_CreateThread(mel__mpmc_producer_fn, "mpmc_prod", &prod_ctx[i]);
    }

    u8* seen = mel_alloc(alloc, MPMC_TOTAL_MP);
    memset(seen, 0, MPMC_TOTAL_MP);

    i32 consumed = 0;
    while (consumed < MPMC_TOTAL_MP)
    {
        void* val = NULL;
        if (mel_mpmc_pop(&q, &val))
        {
            usize idx = (usize)val - 1;
            seen[idx]++;
            consumed++;
        }
    }

    for (i32 i = 0; i < MPMC_PRODUCER_COUNT; i++)
        SDL_WaitThread(producers[i], NULL);

    for (i32 i = 0; i < MPMC_TOTAL_MP; i++)
        MEL_ASSERT_EQ(seen[i], (u8)1);

    mel_dealloc(alloc, seen);
    mel_mpmc_free(&q);
}

MEL_TEST(mpmc_single_producer_multi_consumer, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Mpmc q;
    mel_mpmc_init(&q, 1024, alloc);

    SDL_AtomicInt consumed_count;
    SDL_SetAtomicInt(&consumed_count, 0);
    SDL_SpinLock seen_lock = 0;

    u8* seen = mel_alloc(alloc, MPMC_TOTAL_MP);
    memset(seen, 0, MPMC_TOTAL_MP);

    Mel__Mpmc_Consumer_Ctx cons_ctx[MPMC_CONSUMER_COUNT];
    SDL_Thread* consumers[MPMC_CONSUMER_COUNT];

    for (i32 i = 0; i < MPMC_CONSUMER_COUNT; i++)
    {
        cons_ctx[i] = (Mel__Mpmc_Consumer_Ctx){
            .q = &q,
            .total_items = MPMC_TOTAL_MP,
            .consumed_count = &consumed_count,
            .seen = seen,
            .seen_lock = &seen_lock,
        };
        consumers[i] = SDL_CreateThread(mel__mpmc_consumer_fn, "mpmc_cons", &cons_ctx[i]);
    }

    for (i32 i = 0; i < MPMC_TOTAL_MP; i++)
    {
        usize val = (usize)(i + 1);
        while (!mel_mpmc_push(&q, (void*)val))
            ;
    }

    for (i32 i = 0; i < MPMC_CONSUMER_COUNT; i++)
        SDL_WaitThread(consumers[i], NULL);

    for (i32 i = 0; i < MPMC_TOTAL_MP; i++)
        MEL_ASSERT_EQ(seen[i], (u8)1);

    mel_dealloc(alloc, seen);
    mel_mpmc_free(&q);
}

MEL_TEST(mpmc_full_multi_producer_multi_consumer, .tags = "collection, async")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Mpmc q;
    mel_mpmc_init(&q, 4096, alloc);

    i32 items_per_producer = MPMC_TOTAL_FULL / MPMC_PRODUCER_COUNT;

    Mel__Mpmc_Producer_Ctx prod_ctx[MPMC_PRODUCER_COUNT];
    SDL_Thread* producers[MPMC_PRODUCER_COUNT];

    SDL_AtomicInt consumed_count;
    SDL_SetAtomicInt(&consumed_count, 0);
    SDL_SpinLock seen_lock = 0;

    u8* seen = mel_alloc(alloc, MPMC_TOTAL_FULL);
    memset(seen, 0, MPMC_TOTAL_FULL);

    Mel__Mpmc_Consumer_Ctx cons_ctx[MPMC_CONSUMER_COUNT];
    SDL_Thread* consumers[MPMC_CONSUMER_COUNT];

    for (i32 i = 0; i < MPMC_CONSUMER_COUNT; i++)
    {
        cons_ctx[i] = (Mel__Mpmc_Consumer_Ctx){
            .q = &q,
            .total_items = MPMC_TOTAL_FULL,
            .consumed_count = &consumed_count,
            .seen = seen,
            .seen_lock = &seen_lock,
        };
        consumers[i] = SDL_CreateThread(mel__mpmc_consumer_fn, "mpmc_cons", &cons_ctx[i]);
    }

    for (i32 i = 0; i < MPMC_PRODUCER_COUNT; i++)
    {
        prod_ctx[i] = (Mel__Mpmc_Producer_Ctx){ .q = &q, .producer_id = i, .items_per_producer = items_per_producer };
        producers[i] = SDL_CreateThread(mel__mpmc_producer_fn, "mpmc_prod", &prod_ctx[i]);
    }

    for (i32 i = 0; i < MPMC_PRODUCER_COUNT; i++)
        SDL_WaitThread(producers[i], NULL);

    for (i32 i = 0; i < MPMC_CONSUMER_COUNT; i++)
        SDL_WaitThread(consumers[i], NULL);

    for (i32 i = 0; i < MPMC_TOTAL_FULL; i++)
        MEL_ASSERT_EQ(seen[i], (u8)1);

    mel_dealloc(alloc, seen);
    mel_mpmc_free(&q);
}
