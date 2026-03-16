#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.rcu.h"
#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <string.h>

typedef struct {
    i32 value;
} Rcu_Test_Data;

static Rcu_Test_Data* make_data(const Mel_Alloc* alloc, i32 value)
{
    Rcu_Test_Data* d = mel_alloc(alloc, sizeof(Rcu_Test_Data));
    d->value = value;
    return d;
}

MEL_TEST(rcu_init_destroy, .tags = "collection")
{
    Mel_Rcu rcu;
    mel_rcu_init(&rcu, mel_alloc_heap());

    Mel_Rcu_Token token;
    void* p = mel_rcu_read(&rcu, &token);
    MEL_ASSERT_NULL(p);
    mel_rcu_read_end(&rcu, token);

    mel_rcu_destroy(&rcu);
}

MEL_TEST(rcu_store_and_read, .tags = "collection")
{
    Mel_Rcu rcu;
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_rcu_init(&rcu, alloc);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, make_data(alloc, 42));
    mel_rcu_writer_unlock(&rcu);

    Mel_Rcu_Token token;
    Rcu_Test_Data* d = mel_rcu_read(&rcu, &token);
    MEL_ASSERT_NOT_NULL(d);
    MEL_ASSERT_EQ(d->value, 42);
    mel_rcu_read_end(&rcu, token);

    mel_rcu_destroy(&rcu);
}

MEL_TEST(rcu_update_replaces_value, .tags = "collection")
{
    Mel_Rcu rcu;
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_rcu_init(&rcu, alloc);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, make_data(alloc, 1));
    mel_rcu_writer_unlock(&rcu);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, make_data(alloc, 2));
    mel_rcu_writer_unlock(&rcu);

    Mel_Rcu_Token token;
    Rcu_Test_Data* d = mel_rcu_read(&rcu, &token);
    MEL_ASSERT_EQ(d->value, 2);
    mel_rcu_read_end(&rcu, token);

    mel_rcu_destroy(&rcu);
}

MEL_TEST(rcu_read_during_write_sees_consistent_snapshot, .tags = "collection")
{
    Mel_Rcu rcu;
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_rcu_init(&rcu, alloc);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, make_data(alloc, 10));
    mel_rcu_writer_unlock(&rcu);

    Mel_Rcu_Token token;
    Rcu_Test_Data* snap = mel_rcu_read(&rcu, &token);
    MEL_ASSERT_EQ(snap->value, 10);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, make_data(alloc, 20));
    mel_rcu_writer_unlock(&rcu);

    MEL_ASSERT_EQ(snap->value, 10);

    mel_rcu_read_end(&rcu, token);
    mel_rcu_destroy(&rcu);
}

MEL_TEST(rcu_reentrant_write_during_read, .tags = "collection")
{
    Mel_Rcu rcu;
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_rcu_init(&rcu, alloc);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, make_data(alloc, 1));
    mel_rcu_writer_unlock(&rcu);

    Mel_Rcu_Token token;
    Rcu_Test_Data* snap = mel_rcu_read(&rcu, &token);
    MEL_ASSERT_EQ(snap->value, 1);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, make_data(alloc, 2));
    mel_rcu_writer_unlock(&rcu);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, make_data(alloc, 3));
    mel_rcu_writer_unlock(&rcu);

    MEL_ASSERT_EQ(snap->value, 1);
    mel_rcu_read_end(&rcu, token);

    Mel_Rcu_Token token2;
    Rcu_Test_Data* snap2 = mel_rcu_read(&rcu, &token2);
    MEL_ASSERT_EQ(snap2->value, 3);
    mel_rcu_read_end(&rcu, token2);

    mel_rcu_destroy(&rcu);
}

static _Atomic(i32) s_rcu_sum;

typedef struct {
    Mel_Rcu* rcu;
    i32 reads;
} Rcu_Reader_Arg;

static int rcu_reader_thread(void* data)
{
    Rcu_Reader_Arg* arg = data;
    for (i32 i = 0; i < arg->reads; i++) {
        Mel_Rcu_Token token;
        Rcu_Test_Data* d = mel_rcu_read(arg->rcu, &token);
        if (d)
            atomic_fetch_add(&s_rcu_sum, d->value);
        mel_rcu_read_end(arg->rcu, token);
    }
    return 0;
}

typedef struct {
    Mel_Rcu* rcu;
    const Mel_Alloc* alloc;
    i32 writes;
} Rcu_Writer_Arg;

static int rcu_writer_thread(void* data)
{
    Rcu_Writer_Arg* arg = data;
    for (i32 i = 0; i < arg->writes; i++) {
        mel_rcu_writer_lock(arg->rcu);
        mel_rcu_writer_store(arg->rcu, make_data(arg->alloc, i + 1));
        mel_rcu_writer_unlock(arg->rcu);
    }
    return 0;
}

MEL_TEST(rcu_concurrent_readers, .tags = "collection")
{
    atomic_store(&s_rcu_sum, 0);

    Mel_Rcu rcu;
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_rcu_init(&rcu, alloc);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, make_data(alloc, 1));
    mel_rcu_writer_unlock(&rcu);

    enum { THREADS = 4, READS = 1000 };

    Rcu_Reader_Arg args[THREADS];
    SDL_Thread* threads[THREADS];
    for (i32 t = 0; t < THREADS; t++) {
        args[t] = (Rcu_Reader_Arg){ .rcu = &rcu, .reads = READS };
        threads[t] = SDL_CreateThread(rcu_reader_thread, "rcu_r", &args[t]);
    }
    for (i32 t = 0; t < THREADS; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_EQ(atomic_load(&s_rcu_sum), THREADS * READS);

    mel_rcu_destroy(&rcu);
}

MEL_TEST(rcu_concurrent_readers_and_writer, .tags = "collection")
{
    atomic_store(&s_rcu_sum, 0);

    Mel_Rcu rcu;
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_rcu_init(&rcu, alloc);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, make_data(alloc, 1));
    mel_rcu_writer_unlock(&rcu);

    enum { READER_THREADS = 4, READS = 500, WRITES = 200 };

    Rcu_Reader_Arg reader_args[READER_THREADS];
    Rcu_Writer_Arg writer_arg = { .rcu = &rcu, .alloc = alloc, .writes = WRITES };
    SDL_Thread* threads[READER_THREADS + 1];

    for (i32 t = 0; t < READER_THREADS; t++) {
        reader_args[t] = (Rcu_Reader_Arg){ .rcu = &rcu, .reads = READS };
        threads[t] = SDL_CreateThread(rcu_reader_thread, "rcu_r", &reader_args[t]);
    }
    threads[READER_THREADS] = SDL_CreateThread(rcu_writer_thread, "rcu_w", &writer_arg);

    for (i32 t = 0; t < READER_THREADS + 1; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_GT(atomic_load(&s_rcu_sum), 0);

    mel_rcu_destroy(&rcu);
}

MEL_TEST(rcu_writer_load_returns_current, .tags = "collection")
{
    Mel_Rcu rcu;
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_rcu_init(&rcu, alloc);

    mel_rcu_writer_lock(&rcu);
    MEL_ASSERT_NULL(mel_rcu_writer_load(&rcu));
    mel_rcu_writer_store(&rcu, make_data(alloc, 99));
    Rcu_Test_Data* d = mel_rcu_writer_load(&rcu);
    MEL_ASSERT_EQ(d->value, 99);
    mel_rcu_writer_unlock(&rcu);

    mel_rcu_destroy(&rcu);
}

MEL_TEST(rcu_store_null_clears, .tags = "collection")
{
    Mel_Rcu rcu;
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_rcu_init(&rcu, alloc);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, make_data(alloc, 5));
    mel_rcu_writer_unlock(&rcu);

    mel_rcu_writer_lock(&rcu);
    mel_rcu_writer_store(&rcu, NULL);
    mel_rcu_writer_unlock(&rcu);

    Mel_Rcu_Token token;
    MEL_ASSERT_NULL(mel_rcu_read(&rcu, &token));
    mel_rcu_read_end(&rcu, token);

    mel_rcu_destroy(&rcu);
}
