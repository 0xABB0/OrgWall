#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/allocator.tracking.h"
#include <SDL3/SDL.h>
#include <stdatomic.h>

typedef struct {
    const Mel_Alloc* alloc;
    i32 ops;
    usize alloc_size;
} Tracking_Thread_Arg;

static _Atomic(i32) s_thread_ok;

static int alloc_dealloc_thread(void* data)
{
    Tracking_Thread_Arg* arg = data;
    for (i32 i = 0; i < arg->ops; i++) {
        void* p = mel_alloc(arg->alloc, arg->alloc_size);
        if (!p) return 1;
        *(volatile u8*)p = 0xAB;
        mel_dealloc(arg->alloc, p);
    }
    atomic_fetch_add(&s_thread_ok, 1);
    return 0;
}

MEL_TEST(tracking_mt_concurrent_alloc_dealloc, .tags = "allocator")
{
    atomic_store(&s_thread_ok, 0);

    Mel_Tracking_Allocator tracking;
    mel_tracking_init(&tracking, mel_alloc_heap());
    Mel_Alloc alloc = mel_tracking_allocator(&tracking);

    enum { THREADS = 8, OPS = 2000 };

    Tracking_Thread_Arg args[THREADS];
    SDL_Thread* threads[THREADS];
    for (i32 t = 0; t < THREADS; t++) {
        args[t] = (Tracking_Thread_Arg){ .alloc = &alloc, .ops = OPS, .alloc_size = 64 };
        threads[t] = SDL_CreateThread(alloc_dealloc_thread, "track", &args[t]);
    }
    for (i32 t = 0; t < THREADS; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_EQ(atomic_load(&s_thread_ok), THREADS);
    MEL_ASSERT_EQ(atomic_load(&tracking.current_usage), (usize)0);
    MEL_ASSERT_EQ(atomic_load(&tracking.alloc_count), (u64)(THREADS * OPS));
    MEL_ASSERT_EQ(atomic_load(&tracking.free_count), (u64)(THREADS * OPS));
    MEL_ASSERT_EQ(atomic_load(&tracking.total_allocated), (usize)(THREADS * OPS * 64));
    MEL_ASSERT_EQ(atomic_load(&tracking.total_freed), (usize)(THREADS * OPS * 64));
}

MEL_TEST(tracking_mt_peak_usage_correct, .tags = "allocator")
{
    Mel_Tracking_Allocator tracking;
    mel_tracking_init(&tracking, mel_alloc_heap());
    Mel_Alloc alloc = mel_tracking_allocator(&tracking);

    enum { THREADS = 4, HOLD = 16 };

    void* held[THREADS][HOLD];

    for (i32 t = 0; t < THREADS; t++) {
        for (i32 h = 0; h < HOLD; h++)
            held[t][h] = mel_alloc(&alloc, 128);
    }

    usize peak = atomic_load(&tracking.peak_usage);
    MEL_ASSERT_EQ(peak, (usize)(THREADS * HOLD * 128));

    for (i32 t = 0; t < THREADS; t++)
        for (i32 h = 0; h < HOLD; h++)
            mel_dealloc(&alloc, held[t][h]);

    MEL_ASSERT_EQ(atomic_load(&tracking.current_usage), (usize)0);
    MEL_ASSERT_EQ(atomic_load(&tracking.peak_usage), (usize)(THREADS * HOLD * 128));
}

static int mixed_size_thread(void* data)
{
    Tracking_Thread_Arg* arg = data;
    void* ptrs[8] = {0};
    for (i32 i = 0; i < arg->ops; i++) {
        i32 slot = i % 8;
        if (ptrs[slot]) {
            mel_dealloc(arg->alloc, ptrs[slot]);
            ptrs[slot] = NULL;
        }
        usize size = 16 + (i % 256) * 16;
        ptrs[slot] = mel_alloc(arg->alloc, size);
        if (!ptrs[slot]) return 1;
    }
    for (i32 s = 0; s < 8; s++) {
        if (ptrs[s]) mel_dealloc(arg->alloc, ptrs[s]);
    }
    atomic_fetch_add(&s_thread_ok, 1);
    return 0;
}

MEL_TEST(tracking_mt_mixed_sizes, .tags = "allocator")
{
    atomic_store(&s_thread_ok, 0);

    Mel_Tracking_Allocator tracking;
    mel_tracking_init(&tracking, mel_alloc_heap());
    Mel_Alloc alloc = mel_tracking_allocator(&tracking);

    enum { THREADS = 8, OPS = 2000 };

    Tracking_Thread_Arg args[THREADS];
    SDL_Thread* threads[THREADS];
    for (i32 t = 0; t < THREADS; t++) {
        args[t] = (Tracking_Thread_Arg){ .alloc = &alloc, .ops = OPS, .alloc_size = 0 };
        threads[t] = SDL_CreateThread(mixed_size_thread, "track", &args[t]);
    }
    for (i32 t = 0; t < THREADS; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_EQ(atomic_load(&s_thread_ok), THREADS);
    MEL_ASSERT_EQ(atomic_load(&tracking.current_usage), (usize)0);
    MEL_ASSERT_GT(atomic_load(&tracking.peak_usage), (usize)0);
}

static int realloc_thread(void* data)
{
    Tracking_Thread_Arg* arg = data;
    for (i32 i = 0; i < arg->ops; i++) {
        void* p = mel_alloc(arg->alloc, 32);
        if (!p) return 1;
        p = mel_realloc(arg->alloc, p, 128);
        if (!p) return 1;
        p = mel_realloc(arg->alloc, p, 64);
        if (!p) return 1;
        mel_dealloc(arg->alloc, p);
    }
    atomic_fetch_add(&s_thread_ok, 1);
    return 0;
}

MEL_TEST(tracking_mt_concurrent_realloc, .tags = "allocator")
{
    atomic_store(&s_thread_ok, 0);

    Mel_Tracking_Allocator tracking;
    mel_tracking_init(&tracking, mel_alloc_heap());
    Mel_Alloc alloc = mel_tracking_allocator(&tracking);

    enum { THREADS = 4, OPS = 1000 };

    Tracking_Thread_Arg args[THREADS];
    SDL_Thread* threads[THREADS];
    for (i32 t = 0; t < THREADS; t++) {
        args[t] = (Tracking_Thread_Arg){ .alloc = &alloc, .ops = OPS, .alloc_size = 0 };
        threads[t] = SDL_CreateThread(realloc_thread, "track", &args[t]);
    }
    for (i32 t = 0; t < THREADS; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_EQ(atomic_load(&s_thread_ok), THREADS);
    MEL_ASSERT_EQ(atomic_load(&tracking.current_usage), (usize)0);
    MEL_ASSERT_EQ(atomic_load(&tracking.alloc_count), (u64)(THREADS * OPS * 3));
    MEL_ASSERT_EQ(atomic_load(&tracking.free_count), (u64)(THREADS * OPS * 3));
}
