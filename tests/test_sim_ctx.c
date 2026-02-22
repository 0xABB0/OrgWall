#include "../melody/test.harness.h"
#include "../melody/sim.ctx.h"

#define EVT_MOVE    1
#define EVT_ATTACK  2
#define EVT_SIGNAL  3

typedef struct {
    f32 x, y;
} Move_Event;

typedef struct {
    i32 target;
    i32 damage;
} Attack_Event;

MEL_TEST(sim_init, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 42, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    MEL_ASSERT_EQ(ctx.tick, (u64)0);
    MEL_ASSERT_EQ(ctx.rng.state, (u64)42);
}

MEL_TEST(sim_push_and_iterate_by_type, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    Move_Event move = { .x = 10.0f, .y = 20.0f };
    Attack_Event attack = { .target = 5, .damage = 30 };
    Move_Event move2 = { .x = 99.0f, .y = 88.0f };

    mel_sim_push(&ctx, EVT_MOVE, &move, sizeof(move));
    mel_sim_push(&ctx, EVT_ATTACK, &attack, sizeof(attack));
    mel_sim_push(&ctx, EVT_MOVE, &move2, sizeof(move2));

    Mel_Sim_Iter iter = {0};
    Move_Event* m1 = (Move_Event*)mel_sim_next(&ctx, EVT_MOVE, &iter);
    MEL_ASSERT_NOT_NULL(m1);
    MEL_ASSERT_FLOAT_EQ(m1->x, 10.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(m1->y, 20.0f, 0.001f);

    Move_Event* m2 = (Move_Event*)mel_sim_next(&ctx, EVT_MOVE, &iter);
    MEL_ASSERT_NOT_NULL(m2);
    MEL_ASSERT_FLOAT_EQ(m2->x, 99.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(m2->y, 88.0f, 0.001f);

    MEL_ASSERT_NULL(mel_sim_next(&ctx, EVT_MOVE, &iter));
}

MEL_TEST(sim_iterate_all, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    Move_Event move = { .x = 1.0f, .y = 2.0f };
    Attack_Event attack = { .target = 3, .damage = 4 };

    mel_sim_push(&ctx, EVT_MOVE, &move, sizeof(move));
    mel_sim_push(&ctx, EVT_ATTACK, &attack, sizeof(attack));

    Mel_Sim_Iter iter = {0};
    Mel_Sim_Event event;

    MEL_ASSERT(mel_sim_next_any(&ctx, &iter, &event));
    MEL_ASSERT_EQ(event.type, EVT_MOVE);
    MEL_ASSERT_EQ(event.size, (size)sizeof(Move_Event));
    MEL_ASSERT_NOT_NULL(event.data);
    Move_Event* m = (Move_Event*)event.data;
    MEL_ASSERT_FLOAT_EQ(m->x, 1.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(m->y, 2.0f, 0.001f);

    MEL_ASSERT(mel_sim_next_any(&ctx, &iter, &event));
    MEL_ASSERT_EQ(event.type, EVT_ATTACK);
    MEL_ASSERT_EQ(event.size, (size)sizeof(Attack_Event));
    Attack_Event* a = (Attack_Event*)event.data;
    MEL_ASSERT_EQ(a->target, 3);
    MEL_ASSERT_EQ(a->damage, 4);

    MEL_ASSERT(!mel_sim_next_any(&ctx, &iter, &event));
}

MEL_TEST(sim_clear, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    Move_Event move = { .x = 1.0f, .y = 2.0f };
    mel_sim_push(&ctx, EVT_MOVE, &move, sizeof(move));

    MEL_ASSERT_EQ(ctx.tick, (u64)0);
    mel_sim_clear(&ctx);
    MEL_ASSERT_EQ(ctx.tick, (u64)1);

    Mel_Sim_Iter iter = {0};
    MEL_ASSERT_NULL(mel_sim_next(&ctx, EVT_MOVE, &iter));

    mel_sim_clear(&ctx);
    MEL_ASSERT_EQ(ctx.tick, (u64)2);
}

MEL_TEST(sim_signal_events, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    mel_sim_push(&ctx, EVT_SIGNAL, NULL, 0);

    Mel_Sim_Iter iter = {0};
    void* found = mel_sim_next(&ctx, EVT_SIGNAL, &iter);
    MEL_ASSERT_NOT_NULL(found);
    MEL_ASSERT_NULL(mel_sim_next(&ctx, EVT_SIGNAL, &iter));

    Mel_Sim_Iter iter2 = {0};
    Mel_Sim_Event event;
    MEL_ASSERT(mel_sim_next_any(&ctx, &iter2, &event));
    MEL_ASSERT_EQ(event.type, EVT_SIGNAL);
    MEL_ASSERT_EQ(event.size, (size)0);
    MEL_ASSERT_NULL(event.data);
}

MEL_TEST(sim_rng_determinism, .tags = "sim")
{
    _Alignas(16) u8 buf1[256], buf2[256];
    Mel_Sim_Ctx ctx1, ctx2;

    mel_sim_init(&ctx1, .seed = 12345, .event_buffer = buf1, .event_buffer_size = sizeof(buf1));
    mel_sim_init(&ctx2, .seed = 12345, .event_buffer = buf2, .event_buffer_size = sizeof(buf2));

    for (i32 i = 0; i < 100; i++)
    {
        u64 a = mel_rng_next(&ctx1.rng);
        u64 b = mel_rng_next(&ctx2.rng);
        MEL_ASSERT_EQ(a, b);
    }
}

MEL_TEST(sim_multiple_clears, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    for (i32 tick = 0; tick < 10; tick++)
    {
        Move_Event move = { .x = (f32)tick, .y = (f32)(tick * 2) };
        mel_sim_push(&ctx, EVT_MOVE, &move, sizeof(move));

        Mel_Sim_Iter iter = {0};
        Move_Event* m = (Move_Event*)mel_sim_next(&ctx, EVT_MOVE, &iter);
        MEL_ASSERT_NOT_NULL(m);
        MEL_ASSERT_FLOAT_EQ(m->x, (f32)tick, 0.001f);

        mel_sim_clear(&ctx);
    }

    MEL_ASSERT_EQ(ctx.tick, (u64)10);
}
