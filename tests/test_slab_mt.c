#include "../melody/test.harness.h"
#include "../melody/collection.slab.h"
#include <SDL3/SDL.h>
#include <stdatomic.h>

static _Atomic(i32) s_thread_ok;

typedef struct {
    Mel_Slab_Alloc* slab;
    i32 ops;
} Slab_Thread_Arg;

static int slab_alloc_free_thread(void* data)
{
    Slab_Thread_Arg* arg = data;
    for (i32 i = 0; i < arg->ops; i++) {
        usize size = 8 + (i % 3) * 24;
        void* p = mel_slab_alloc(arg->slab, size);
        if (!p) return 1;
        *(volatile u8*)p = 0xCD;
        mel_slab_free(arg->slab, p);
    }
    atomic_fetch_add(&s_thread_ok, 1);
    return 0;
}

MEL_TEST(slab_mt_concurrent_alloc_free, .tags = "allocator")
{
    atomic_store(&s_thread_ok, 0);

    _Alignas(16) u8 buf_small[16 * 256];
    _Alignas(16) u8 buf_medium[32 * 128];
    _Alignas(16) u8 buf_large[64 * 128];

    Mel_Slab_Class classes[3];
    Mel_Slab_Class_Desc descs[] = {
        { .buffer = buf_small, .buffer_size = sizeof(buf_small), .block_size = 16 },
        { .buffer = buf_medium, .buffer_size = sizeof(buf_medium), .block_size = 32 },
        { .buffer = buf_large, .buffer_size = sizeof(buf_large), .block_size = 64 },
    };

    Mel_Slab_Alloc slab;
    mel_slab_init(&slab, classes, descs, 3);

    enum { THREADS = 8, OPS = 2000 };

    Slab_Thread_Arg args[THREADS];
    SDL_Thread* threads[THREADS];
    for (i32 t = 0; t < THREADS; t++) {
        args[t] = (Slab_Thread_Arg){ .slab = &slab, .ops = OPS };
        threads[t] = SDL_CreateThread(slab_alloc_free_thread, "slab_mt", &args[t]);
    }
    for (i32 t = 0; t < THREADS; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_EQ(atomic_load(&s_thread_ok), THREADS);
    MEL_ASSERT_EQ(atomic_load(&slab.classes[0].pool.used_count), (usize)0);
    MEL_ASSERT_EQ(atomic_load(&slab.classes[1].pool.used_count), (usize)0);
    MEL_ASSERT_EQ(atomic_load(&slab.classes[2].pool.used_count), (usize)0);
}

static int slab_hold_thread(void* data)
{
    Slab_Thread_Arg* arg = data;
    void* held[8] = {0};
    for (i32 i = 0; i < arg->ops; i++) {
        i32 slot = i % 8;
        if (held[slot]) {
            mel_slab_free(arg->slab, held[slot]);
            held[slot] = NULL;
        }
        usize size = 8 + (i % 3) * 24;
        held[slot] = mel_slab_alloc(arg->slab, size);
        if (!held[slot]) return 1;
    }
    for (i32 s = 0; s < 8; s++) {
        if (held[s]) mel_slab_free(arg->slab, held[s]);
    }
    atomic_fetch_add(&s_thread_ok, 1);
    return 0;
}

MEL_TEST(slab_mt_mixed_classes, .tags = "allocator")
{
    atomic_store(&s_thread_ok, 0);

    _Alignas(16) u8 buf_small[16 * 256];
    _Alignas(16) u8 buf_large[64 * 256];

    Mel_Slab_Class classes[2];
    Mel_Slab_Class_Desc descs[] = {
        { .buffer = buf_small, .buffer_size = sizeof(buf_small), .block_size = 16 },
        { .buffer = buf_large, .buffer_size = sizeof(buf_large), .block_size = 64 },
    };

    Mel_Slab_Alloc slab;
    mel_slab_init(&slab, classes, descs, 2);

    enum { THREADS = 4, OPS = 1000 };

    Slab_Thread_Arg args[THREADS];
    SDL_Thread* threads[THREADS];
    for (i32 t = 0; t < THREADS; t++) {
        args[t] = (Slab_Thread_Arg){ .slab = &slab, .ops = OPS };
        threads[t] = SDL_CreateThread(slab_hold_thread, "slab_mt", &args[t]);
    }
    for (i32 t = 0; t < THREADS; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_EQ(atomic_load(&s_thread_ok), THREADS);
    MEL_ASSERT_EQ(atomic_load(&slab.classes[0].pool.used_count), (usize)0);
    MEL_ASSERT_EQ(atomic_load(&slab.classes[1].pool.used_count), (usize)0);
}
