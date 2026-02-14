#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/event.channel.h"

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

MEL_TEST(init_destroy)
{
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    MEL_ASSERT_EQ(ch.subs.count, (usize)0);
    MEL_ASSERT_EQ(ch.next_id, (u32)1);
    MEL_ASSERT_EQ(ch.firing, false);

    mel_event_channel_destroy(&ch);
    MEL_ASSERT_EQ(ch.subs.count, (usize)0);
    MEL_ASSERT_EQ(ch.next_id, (u32)0);
    MEL_PASS();
}

MEL_TEST(single_subscriber)
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
    MEL_PASS();
}

MEL_TEST(multiple_subscribers)
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
    MEL_PASS();
}

MEL_TEST(fire_preserves_order)
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
    MEL_PASS();
}

MEL_TEST(unsubscribe)
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
    MEL_PASS();
}

MEL_TEST(unsubscribe_preserves_order)
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
    MEL_PASS();
}

MEL_TEST(fire_no_subscribers)
{
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_fire(&ch, &(Hit_Event){ .damage = 99, .source = 0 });

    mel_event_channel_destroy(&ch);
    MEL_PASS();
}

MEL_TEST(fire_null_event)
{
    i32 counter = 0;
    Mel_Event_Channel ch;
    mel_event_channel_init(&ch, mel_alloc_heap());

    mel_event_channel_on(&ch, on_signal, &counter);
    mel_event_channel_fire(&ch, NULL);

    MEL_ASSERT_EQ(counter, 1);

    mel_event_channel_destroy(&ch);
    MEL_PASS();
}

MEL_TEST(multiple_fires)
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
    MEL_PASS();
}

MEL_TEST(context_passed_correctly)
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
    MEL_PASS();
}

MEL_TEST(sub_handle_uniqueness)
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
    MEL_PASS();
}

MEL_TEST(unsubscribe_all)
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
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Event Channel Tests");

    MEL_RUN_TEST(init_destroy);
    MEL_RUN_TEST(single_subscriber);
    MEL_RUN_TEST(multiple_subscribers);
    MEL_RUN_TEST(fire_preserves_order);
    MEL_RUN_TEST(unsubscribe);
    MEL_RUN_TEST(unsubscribe_preserves_order);
    MEL_RUN_TEST(fire_no_subscribers);
    MEL_RUN_TEST(fire_null_event);
    MEL_RUN_TEST(multiple_fires);
    MEL_RUN_TEST(context_passed_correctly);
    MEL_RUN_TEST(sub_handle_uniqueness);
    MEL_RUN_TEST(unsubscribe_all);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
