#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/event.channel.h"
#include <SDL3/SDL.h>
#include <stdatomic.h>

typedef struct {
    i32 damage;
    u64 source;
} Hit_Event;

static i32 s_call_count;
static i32 s_last_damage;
static u64 s_last_source;
static i32 s_call_order[8];

static void reset_state(void)
{
    s_call_count = 0;
    s_last_damage = 0;
    s_last_source = 0;
    memset(s_call_order, 0, sizeof(s_call_order));
}

static void on_hit(void* ctx, const void* event)
{
    (void)ctx;
    const Hit_Event* e = event;
    s_last_damage = e->damage;
    s_last_source = e->source;
    s_call_count++;
}

static void on_hit_ordered(void* ctx, const void* event)
{
    (void)event;
    i32 order = *(i32*)ctx;
    s_call_order[s_call_count] = order;
    s_call_count++;
}

static void on_signal(void* ctx, const void* event)
{
    (void)event;
    i32* counter = ctx;
    (*counter)++;
}

MEL_TEST(init_destroy, .tags = "event")
{
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    MEL_ASSERT_EQ(mel_event_channel_count(&ch), (u32)0);
    MEL_ASSERT_EQ(ch.next_id, (u32)1);
    MEL_ASSERT_NOT_NULL(ch.rcu.writer_lock);

    mel_event_channel_destroy(&ch);
    MEL_ASSERT_EQ(ch.next_id, (u32)0);
    MEL_ASSERT_NULL(ch.rcu.writer_lock);
}

MEL_TEST(single_subscriber, .tags = "event")
{
    reset_state();
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_on(&ch, on_hit, NULL);
    mel_event_channel_fire(&ch, &(Hit_Event){ .damage = 42, .source = 7 });

    MEL_ASSERT_EQ(s_call_count, 1);
    MEL_ASSERT_EQ(s_last_damage, 42);
    MEL_ASSERT_EQ(s_last_source, (u64)7);

    mel_event_channel_destroy(&ch);
}

MEL_TEST(multiple_subscribers, .tags = "event")
{
    reset_state();
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_on(&ch, on_hit, NULL);
    mel_event_channel_on(&ch, on_hit, NULL);
    mel_event_channel_on(&ch, on_hit, NULL);

    mel_event_channel_fire(&ch, &(Hit_Event){ .damage = 10, .source = 1 });

    MEL_ASSERT_EQ(s_call_count, 3);

    mel_event_channel_destroy(&ch);
}

MEL_TEST(fire_preserves_order, .tags = "event")
{
    reset_state();
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    i32 a = 1, b = 2, c = 3;
    mel_event_channel_on(&ch, on_hit_ordered, &a);
    mel_event_channel_on(&ch, on_hit_ordered, &b);
    mel_event_channel_on(&ch, on_hit_ordered, &c);

    mel_event_channel_fire(&ch, &(Hit_Event){0});

    MEL_ASSERT_EQ(s_call_order[0], 1);
    MEL_ASSERT_EQ(s_call_order[1], 2);
    MEL_ASSERT_EQ(s_call_order[2], 3);

    mel_event_channel_destroy(&ch);
}

MEL_TEST(unsubscribe, .tags = "event")
{
    reset_state();
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    Mel_Event_Sub sub1 = mel_event_channel_on(&ch, on_hit, NULL);
    mel_event_channel_on(&ch, on_hit, NULL);

    mel_event_channel_off(&ch, sub1);
    mel_event_channel_fire(&ch, &(Hit_Event){ .damage = 5, .source = 0 });

    MEL_ASSERT_EQ(s_call_count, 1);

    mel_event_channel_destroy(&ch);
}

MEL_TEST(unsubscribe_preserves_order, .tags = "event")
{
    reset_state();
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    i32 a = 1, b = 2, c = 3;
    mel_event_channel_on(&ch, on_hit_ordered, &a);
    Mel_Event_Sub sub_b = mel_event_channel_on(&ch, on_hit_ordered, &b);
    mel_event_channel_on(&ch, on_hit_ordered, &c);

    mel_event_channel_off(&ch, sub_b);
    mel_event_channel_fire(&ch, &(Hit_Event){0});

    MEL_ASSERT_EQ(s_call_count, 2);
    MEL_ASSERT_EQ(s_call_order[0], 1);
    MEL_ASSERT_EQ(s_call_order[1], 3);

    mel_event_channel_destroy(&ch);
}

MEL_TEST(fire_no_subscribers, .tags = "event")
{
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_fire(&ch, &(Hit_Event){ .damage = 99, .source = 0 });

    mel_event_channel_destroy(&ch);
}

MEL_TEST(fire_null_event, .tags = "event")
{
    i32 counter = 0;
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_on(&ch, on_signal, &counter);
    mel_event_channel_fire(&ch, NULL);

    MEL_ASSERT_EQ(counter, 1);

    mel_event_channel_destroy(&ch);
}

MEL_TEST(multiple_fires, .tags = "event")
{
    reset_state();
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_on(&ch, on_hit, NULL);

    mel_event_channel_fire(&ch, &(Hit_Event){ .damage = 10, .source = 1 });
    MEL_ASSERT_EQ(s_call_count, 1);
    MEL_ASSERT_EQ(s_last_damage, 10);

    mel_event_channel_fire(&ch, &(Hit_Event){ .damage = 20, .source = 2 });
    MEL_ASSERT_EQ(s_call_count, 2);
    MEL_ASSERT_EQ(s_last_damage, 20);

    mel_event_channel_fire(&ch, &(Hit_Event){ .damage = 30, .source = 3 });
    MEL_ASSERT_EQ(s_call_count, 3);
    MEL_ASSERT_EQ(s_last_damage, 30);

    mel_event_channel_destroy(&ch);
}

MEL_TEST(context_passed_correctly, .tags = "event")
{
    i32 counter_a = 0;
    i32 counter_b = 0;
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_on(&ch, on_signal, &counter_a);
    mel_event_channel_on(&ch, on_signal, &counter_b);

    mel_event_channel_fire(&ch, NULL);
    mel_event_channel_fire(&ch, NULL);

    MEL_ASSERT_EQ(counter_a, 2);
    MEL_ASSERT_EQ(counter_b, 2);

    mel_event_channel_destroy(&ch);
}

MEL_TEST(sub_handle_uniqueness, .tags = "event")
{
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    Mel_Event_Sub s1 = mel_event_channel_on(&ch, on_hit, NULL);
    Mel_Event_Sub s2 = mel_event_channel_on(&ch, on_hit, NULL);
    Mel_Event_Sub s3 = mel_event_channel_on(&ch, on_hit, NULL);

    MEL_ASSERT_NEQ(s1.id, s2.id);
    MEL_ASSERT_NEQ(s2.id, s3.id);
    MEL_ASSERT_NEQ(s1.id, s3.id);

    mel_event_channel_destroy(&ch);
}

MEL_TEST(unsubscribe_all, .tags = "event")
{
    reset_state();
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    Mel_Event_Sub s1 = mel_event_channel_on(&ch, on_hit, NULL);
    Mel_Event_Sub s2 = mel_event_channel_on(&ch, on_hit, NULL);
    Mel_Event_Sub s3 = mel_event_channel_on(&ch, on_hit, NULL);

    mel_event_channel_off(&ch, s1);
    mel_event_channel_off(&ch, s2);
    mel_event_channel_off(&ch, s3);

    mel_event_channel_fire(&ch, &(Hit_Event){ .damage = 1, .source = 0 });
    MEL_ASSERT_EQ(s_call_count, 0);

    mel_event_channel_destroy(&ch);
}

static void on_noop(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;
}

static void on_subscribe_during_fire(void* ctx, const void* event)
{
    (void)event;
    Mel_Event_Channel* ch = ctx;
    mel_event_channel_on(ch, on_noop, NULL);
}

MEL_TEST(subscribe_during_fire, .tags = "event")
{
    i32 counter = 0;
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_on(&ch, on_subscribe_during_fire, &ch);
    mel_event_channel_on(&ch, on_signal, &counter);

    mel_event_channel_fire(&ch, NULL);

    MEL_ASSERT_EQ(counter, 1);
    MEL_ASSERT_EQ(mel_event_channel_count(&ch), (u32)3);

    mel_event_channel_fire(&ch, NULL);
    MEL_ASSERT_EQ(mel_event_channel_count(&ch), (u32)4);

    mel_event_channel_destroy(&ch);
}

static Mel_Event_Sub s_unsub_target;

static void on_unsubscribe_during_fire(void* ctx, const void* event)
{
    (void)event;
    Mel_Event_Channel* ch = ctx;
    mel_event_channel_off(ch, s_unsub_target);
}

MEL_TEST(unsubscribe_during_fire, .tags = "event")
{
    i32 counter = 0;
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_on(&ch, on_unsubscribe_during_fire, &ch);
    s_unsub_target = mel_event_channel_on(&ch, on_signal, &counter);

    mel_event_channel_fire(&ch, NULL);

    MEL_ASSERT_EQ(counter, 1);
    MEL_ASSERT_EQ(mel_event_channel_count(&ch), (u32)1);

    mel_event_channel_destroy(&ch);
}

static _Atomic(i32) s_atomic_counter;

static void on_atomic_inc(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;
    atomic_fetch_add(&s_atomic_counter, 1);
}

typedef struct {
    Mel_Event_Channel* ch;
    i32 fires;
} Fire_Thread_Arg;

static int fire_thread_fn(void* data)
{
    Fire_Thread_Arg* arg = data;
    for (i32 i = 0; i < arg->fires; i++)
        mel_event_channel_fire(arg->ch, NULL);
    return 0;
}

MEL_TEST(concurrent_fire_stress, .tags = "event")
{
    atomic_store(&s_atomic_counter, 0);

    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_on(&ch, on_atomic_inc, NULL);
    mel_event_channel_on(&ch, on_atomic_inc, NULL);

    enum { THREAD_COUNT = 4, FIRES_PER_THREAD = 500 };

    Fire_Thread_Arg args[THREAD_COUNT];
    SDL_Thread* threads[THREAD_COUNT];
    for (i32 t = 0; t < THREAD_COUNT; t++) {
        args[t] = (Fire_Thread_Arg){ .ch = &ch, .fires = FIRES_PER_THREAD };
        threads[t] = SDL_CreateThread(fire_thread_fn, "fire", &args[t]);
    }
    for (i32 t = 0; t < THREAD_COUNT; t++)
        SDL_WaitThread(threads[t], NULL);

    i32 expected = THREAD_COUNT * FIRES_PER_THREAD * 2;
    MEL_ASSERT_EQ(atomic_load(&s_atomic_counter), expected);

    mel_event_channel_destroy(&ch);
}

typedef struct {
    Mel_Event_Channel* ch;
    i32 ops;
} Sub_Thread_Arg;

static int sub_unsub_thread_fn(void* data)
{
    Sub_Thread_Arg* arg = data;
    for (i32 i = 0; i < arg->ops; i++) {
        Mel_Event_Sub sub = mel_event_channel_on(arg->ch, on_atomic_inc, NULL);
        mel_event_channel_off(arg->ch, sub);
    }
    return 0;
}

MEL_TEST(concurrent_sub_unsub_during_fire, .tags = "event")
{
    atomic_store(&s_atomic_counter, 0);

    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_on(&ch, on_atomic_inc, NULL);

    enum { FIRE_THREADS = 2, SUB_THREADS = 2, OPS = 500 };

    Fire_Thread_Arg fire_args[FIRE_THREADS];
    Sub_Thread_Arg sub_args[SUB_THREADS];
    SDL_Thread* threads[FIRE_THREADS + SUB_THREADS];

    for (i32 t = 0; t < FIRE_THREADS; t++) {
        fire_args[t] = (Fire_Thread_Arg){ .ch = &ch, .fires = OPS };
        threads[t] = SDL_CreateThread(fire_thread_fn, "fire", &fire_args[t]);
    }
    for (i32 t = 0; t < SUB_THREADS; t++) {
        sub_args[t] = (Sub_Thread_Arg){ .ch = &ch, .ops = OPS };
        threads[FIRE_THREADS + t] = SDL_CreateThread(sub_unsub_thread_fn, "sub", &sub_args[t]);
    }
    for (i32 t = 0; t < FIRE_THREADS + SUB_THREADS; t++)
        SDL_WaitThread(threads[t], NULL);

    MEL_ASSERT_GE(atomic_load(&s_atomic_counter), FIRE_THREADS * OPS);

    MEL_ASSERT_EQ(mel_event_channel_count(&ch), (u32)1);

    mel_event_channel_destroy(&ch);
}

MEL_TEST(fire_from_multiple_channels_concurrently, .tags = "event")
{
    atomic_store(&s_atomic_counter, 0);

    enum { CHANNEL_COUNT = 4, FIRES = 500 };

    Mel_Event_Channel channels[CHANNEL_COUNT];
    Fire_Thread_Arg args[CHANNEL_COUNT];
    SDL_Thread* threads[CHANNEL_COUNT];

    for (i32 c = 0; c < CHANNEL_COUNT; c++) {
        mel_event_channel_init(&channels[c], mel_alloc_heap());
        mel_event_channel_on(&channels[c], on_atomic_inc, NULL);
        args[c] = (Fire_Thread_Arg){ .ch = &channels[c], .fires = FIRES };
        threads[c] = SDL_CreateThread(fire_thread_fn, "fire", &args[c]);
    }
    for (i32 c = 0; c < CHANNEL_COUNT; c++)
        SDL_WaitThread(threads[c], NULL);

    MEL_ASSERT_EQ(atomic_load(&s_atomic_counter), CHANNEL_COUNT * FIRES);

    for (i32 c = 0; c < CHANNEL_COUNT; c++)
        mel_event_channel_destroy(&channels[c]);
}
