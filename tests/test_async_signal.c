#include "../melody/test.harness.h"
#include "../melody/async.signal.h"

#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_atomic.h>

MEL_TEST(signal_init_green, .tags = "async")
{
    Mel_Signal s = MEL_SIGNAL_INIT;
    i32 state = atomic_load(&s.state);
    MEL_ASSERT_EQ(mel__signal_counter(state), (u16)0);
    MEL_ASSERT_EQ(mel__signal_head(state), (u16)MEL_SIGNAL_NULL_INDEX);
    MEL_ASSERT_EQ(s.generation, (u32)0);
}

MEL_TEST(signal_set_turns_red, .tags = "async")
{
    Mel_Signal s = MEL_SIGNAL_INIT;
    mel_signal_set(&s);
    i32 state = atomic_load(&s.state);
    MEL_ASSERT_GT(mel__signal_counter(state), (u16)0);
}

MEL_TEST(signal_set_increments_generation, .tags = "async")
{
    Mel_Signal s = MEL_SIGNAL_INIT;
    u32 gen_before = s.generation;
    mel_signal_set(&s);
    MEL_ASSERT_GT(s.generation, gen_before);
}

MEL_TEST(signal_clear_turns_green, .tags = "async")
{
    Mel_Signal s = MEL_SIGNAL_INIT;
    mel_signal_set(&s);
    mel_signal_clear(&s);
    i32 state = atomic_load(&s.state);
    MEL_ASSERT_EQ(mel__signal_counter(state), (u16)0);
    MEL_ASSERT_EQ(mel__signal_head(state), (u16)MEL_SIGNAL_NULL_INDEX);
}

MEL_TEST(signal_double_set_idempotent, .tags = "async")
{
    Mel_Signal s = MEL_SIGNAL_INIT;
    mel_signal_set(&s);
    u32 gen_after_first = s.generation;
    mel_signal_set(&s);
    i32 state = atomic_load(&s.state);
    MEL_ASSERT_EQ(mel__signal_counter(state), (u16)1);
    MEL_ASSERT_EQ(s.generation, gen_after_first);
}

MEL_TEST(signal_clear_on_green_noop, .tags = "async")
{
    Mel_Signal s = MEL_SIGNAL_INIT;
    mel_signal_clear(&s);
    i32 state = atomic_load(&s.state);
    MEL_ASSERT_EQ(mel__signal_counter(state), (u16)0);
}

MEL_TEST(signal_generation_tracks_transitions, .tags = "async")
{
    Mel_Signal s = MEL_SIGNAL_INIT;

    mel_signal_set(&s);
    u32 g1 = s.generation;

    mel_signal_clear(&s);
    u32 g2 = s.generation;
    MEL_ASSERT_EQ(g1, g2);

    mel_signal_set(&s);
    u32 g3 = s.generation;
    MEL_ASSERT_NEQ(g1, g3);
}

MEL_TEST(signal_pack_unpack_roundtrip, .tags = "async")
{
    for (u16 c = 0; c < 100; c++)
    {
        for (u16 h = 0; h < 100; h++)
        {
            i32 packed = mel__signal_pack(c, h);
            MEL_ASSERT_EQ(mel__signal_counter(packed), c);
            MEL_ASSERT_EQ(mel__signal_head(packed), h);
        }
    }

    i32 max_packed = mel__signal_pack(0xFFFF, 0xFFFF);
    MEL_ASSERT_EQ(mel__signal_counter(max_packed), (u16)0xFFFF);
    MEL_ASSERT_EQ(mel__signal_head(max_packed), (u16)0xFFFF);
}

MEL_TEST(counter_increment_decrement, .tags = "async")
{
    Mel_Counter c = MEL_COUNTER_INIT;

    for (i32 i = 0; i < 5; i++)
        mel_counter_increment(&c);

    i32 state = atomic_load(&c.signal.state);
    MEL_ASSERT_EQ(mel__signal_counter(state), (u16)5);

    for (i32 i = 0; i < 5; i++)
        mel_counter_decrement(&c);

    state = atomic_load(&c.signal.state);
    MEL_ASSERT_EQ(mel__signal_counter(state), (u16)0);
}

MEL_TEST(counter_generation_on_first_increment, .tags = "async")
{
    Mel_Counter c = MEL_COUNTER_INIT;
    u32 gen_before = c.signal.generation;
    mel_counter_increment(&c);
    MEL_ASSERT_GT(c.signal.generation, gen_before);

    u32 gen_after_first = c.signal.generation;
    mel_counter_increment(&c);
    MEL_ASSERT_EQ(c.signal.generation, gen_after_first);
}

typedef struct {
    Mel_Signal* signal;
    i32 iterations;
} Mel__Signal_Stress_Data;

static int mel__signal_stress_thread(void* arg)
{
    Mel__Signal_Stress_Data* d = (Mel__Signal_Stress_Data*)arg;
    for (i32 i = 0; i < d->iterations; i++)
    {
        mel_signal_set(d->signal);
        mel_signal_clear(d->signal);
    }
    return 0;
}

MEL_TEST(signal_multithread_set_clear_stress, .tags = "async")
{
    Mel_Signal s = MEL_SIGNAL_INIT;
    enum { NUM_THREADS = 8, ITERATIONS = 10000 };

    Mel__Signal_Stress_Data data = { .signal = &s, .iterations = ITERATIONS };
    SDL_Thread* threads[NUM_THREADS];

    for (i32 i = 0; i < NUM_THREADS; i++)
        threads[i] = SDL_CreateThread(mel__signal_stress_thread, "stress", &data);

    for (i32 i = 0; i < NUM_THREADS; i++)
        SDL_WaitThread(threads[i], NULL);

    i32 state = atomic_load(&s.state);
    u16 counter = mel__signal_counter(state);
    MEL_ASSERT(counter == 0 || counter == 1);
}

typedef struct {
    Mel_Counter* counter;
    i32 increments;
} Mel__Counter_Stress_Data;

static int mel__counter_inc_thread(void* arg)
{
    Mel__Counter_Stress_Data* d = (Mel__Counter_Stress_Data*)arg;
    for (i32 i = 0; i < d->increments; i++)
        mel_counter_increment(d->counter);
    return 0;
}

static int mel__counter_dec_thread(void* arg)
{
    Mel__Counter_Stress_Data* d = (Mel__Counter_Stress_Data*)arg;
    for (i32 i = 0; i < d->increments; i++)
        mel_counter_decrement(d->counter);
    return 0;
}

MEL_TEST(counter_multithread_increment, .tags = "async")
{
    Mel_Counter c = MEL_COUNTER_INIT;
    enum { NUM_THREADS = 8, INCREMENTS = 1000 };

    Mel__Counter_Stress_Data data = { .counter = &c, .increments = INCREMENTS };
    SDL_Thread* threads[NUM_THREADS];

    for (i32 i = 0; i < NUM_THREADS; i++)
        threads[i] = SDL_CreateThread(mel__counter_inc_thread, "inc", &data);

    for (i32 i = 0; i < NUM_THREADS; i++)
        SDL_WaitThread(threads[i], NULL);

    i32 state = atomic_load(&c.signal.state);
    MEL_ASSERT_EQ(mel__signal_counter(state), (u16)(NUM_THREADS * INCREMENTS));

    for (i32 i = 0; i < NUM_THREADS; i++)
        threads[i] = SDL_CreateThread(mel__counter_dec_thread, "dec", &data);

    for (i32 i = 0; i < NUM_THREADS; i++)
        SDL_WaitThread(threads[i], NULL);

    state = atomic_load(&c.signal.state);
    MEL_ASSERT_EQ(mel__signal_counter(state), (u16)0);
}

typedef struct {
    Mel_Counter* counter;
    i32 ops;
} Mel__Mixed_Stress_Data;

static int mel__counter_mixed_thread(void* arg)
{
    Mel__Mixed_Stress_Data* d = (Mel__Mixed_Stress_Data*)arg;
    for (i32 i = 0; i < d->ops; i++)
        mel_counter_increment(d->counter);
    for (i32 i = 0; i < d->ops; i++)
        mel_counter_decrement(d->counter);
    return 0;
}

MEL_TEST(counter_mixed_inc_dec_stress, .tags = "async")
{
    Mel_Counter c = MEL_COUNTER_INIT;
    enum { NUM_THREADS = 4, OPS = 5000 };

    mel_counter_increment(&c);

    Mel__Mixed_Stress_Data md = { .counter = &c, .ops = OPS };

    SDL_Thread* threads[NUM_THREADS];
    for (i32 i = 0; i < NUM_THREADS; i++)
        threads[i] = SDL_CreateThread(mel__counter_mixed_thread, "mix", &md);

    for (i32 i = 0; i < NUM_THREADS; i++)
        SDL_WaitThread(threads[i], NULL);

    i32 state = atomic_load(&c.signal.state);
    MEL_ASSERT_EQ(mel__signal_counter(state), (u16)1);

    mel_counter_decrement(&c);
    state = atomic_load(&c.signal.state);
    MEL_ASSERT_EQ(mel__signal_counter(state), (u16)0);
}
