#include "../melody/test.harness.h"
#include "../melody/collection.pool.h"
#include <SDL3/SDL.h>
#include <stdatomic.h>

static _Atomic(i32) s_thread_ok;

typedef struct {
    Mel_Pool* pool;
    i32 ops;
} Pool_Thread_Arg;

static int alloc_free_thread(void* data)
{
    Pool_Thread_Arg* arg = data;
    for (i32 i = 0; i < arg->ops; i++) {
        void* p = mel_pool_alloc(arg->pool);
        if (!p) return 1;
        *(volatile u8*)p = 0xAB;
        mel_pool_free(arg->pool, p);
    }
    atomic_fetch_add(&s_thread_ok, 1);
    return 0;
}

MEL_TEST(pool_mt_concurrent_alloc_free, .tags = "allocator")
{
    atomic_store(&s_thread_ok, 0);

    _Alignas(16) u8 buffer[64 * 256];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = 64);

    enum { THREADS = 8, OPS = 2000 };

    Pool_Thread_Arg args[THREADS];
    SDL_Thread* threads[THREADS];
    for (i32 t = 0; t < THREADS; t++) {
        args[t] = (Pool_Thread_Arg){ .pool = &pool, .ops = OPS };
        threads[t] = SDL_CreateThread(alloc_free_thread, "pool_mt", &args[t]);
    }
    for (i32 t = 0; t < THREADS; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_EQ(atomic_load(&s_thread_ok), THREADS);
    MEL_ASSERT_EQ(atomic_load(&pool.used_count), (usize)0);
}

static int hold_and_release_thread(void* data)
{
    Pool_Thread_Arg* arg = data;
    void* held[4] = {0};
    for (i32 i = 0; i < arg->ops; i++) {
        i32 slot = i % 4;
        if (held[slot]) {
            mel_pool_free(arg->pool, held[slot]);
            held[slot] = NULL;
        }
        held[slot] = mel_pool_alloc(arg->pool);
        if (!held[slot]) return 1;
        *(volatile u8*)held[slot] = (u8)i;
    }
    for (i32 s = 0; s < 4; s++) {
        if (held[s]) mel_pool_free(arg->pool, held[s]);
    }
    atomic_fetch_add(&s_thread_ok, 1);
    return 0;
}

MEL_TEST(pool_mt_hold_and_release, .tags = "allocator")
{
    atomic_store(&s_thread_ok, 0);

    _Alignas(16) u8 buffer[64 * 128];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = 64);

    enum { THREADS = 4, OPS = 1000 };

    Pool_Thread_Arg args[THREADS];
    SDL_Thread* threads[THREADS];
    for (i32 t = 0; t < THREADS; t++) {
        args[t] = (Pool_Thread_Arg){ .pool = &pool, .ops = OPS };
        threads[t] = SDL_CreateThread(hold_and_release_thread, "pool_mt", &args[t]);
    }
    for (i32 t = 0; t < THREADS; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_EQ(atomic_load(&s_thread_ok), THREADS);
    MEL_ASSERT_EQ(atomic_load(&pool.used_count), (usize)0);
}

static int no_duplicate_thread(void* data)
{
    Pool_Thread_Arg* arg = data;
    void* ptrs[16];
    for (i32 i = 0; i < 16; i++) {
        ptrs[i] = mel_pool_alloc(arg->pool);
        if (!ptrs[i]) return 1;
    }
    for (i32 i = 0; i < 16; i++) {
        for (i32 j = i + 1; j < 16; j++) {
            if (ptrs[i] == ptrs[j]) return 1;
        }
    }
    for (i32 i = 0; i < 16; i++)
        mel_pool_free(arg->pool, ptrs[i]);
    atomic_fetch_add(&s_thread_ok, 1);
    return 0;
}

MEL_TEST(pool_mt_no_duplicate_allocs, .tags = "allocator")
{
    atomic_store(&s_thread_ok, 0);

    _Alignas(16) u8 buffer[64 * 256];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = 64);

    enum { THREADS = 8 };

    Pool_Thread_Arg args[THREADS];
    SDL_Thread* threads[THREADS];
    for (i32 t = 0; t < THREADS; t++) {
        args[t] = (Pool_Thread_Arg){ .pool = &pool, .ops = 0 };
        threads[t] = SDL_CreateThread(no_duplicate_thread, "pool_mt", &args[t]);
    }
    for (i32 t = 0; t < THREADS; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_EQ(atomic_load(&s_thread_ok), THREADS);
    MEL_ASSERT_EQ(atomic_load(&pool.used_count), (usize)0);
}

static int data_integrity_thread(void* data)
{
    Pool_Thread_Arg* arg = data;
    for (i32 i = 0; i < arg->ops; i++) {
        u64* p = (u64*)mel_pool_alloc(arg->pool);
        if (!p) return 1;
        *p = 0xDEADBEEFCAFEBABEull;
        if (*p != 0xDEADBEEFCAFEBABEull) return 1;
        mel_pool_free(arg->pool, p);
    }
    atomic_fetch_add(&s_thread_ok, 1);
    return 0;
}

MEL_TEST(pool_mt_data_integrity, .tags = "allocator")
{
    atomic_store(&s_thread_ok, 0);

    _Alignas(16) u8 buffer[64 * 256];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = 64);

    enum { THREADS = 8, OPS = 2000 };

    Pool_Thread_Arg args[THREADS];
    SDL_Thread* threads[THREADS];
    for (i32 t = 0; t < THREADS; t++) {
        args[t] = (Pool_Thread_Arg){ .pool = &pool, .ops = OPS };
        threads[t] = SDL_CreateThread(data_integrity_thread, "pool_mt", &args[t]);
    }
    for (i32 t = 0; t < THREADS; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_EQ(atomic_load(&s_thread_ok), THREADS);
    MEL_ASSERT_EQ(atomic_load(&pool.used_count), (usize)0);
}
