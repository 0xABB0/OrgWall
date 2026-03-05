#include "../melody/test.harness.h"
#include "../melody/sim.ctx.h"
#include "../melody/allocator.heap.h"

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
    MEL_ASSERT_FLOAT_EQ(ctx.time_scale, 1.0f, 0.001f);

    mel_sim_shutdown(&ctx);
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

    mel_sim_shutdown(&ctx);
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

    mel_sim_shutdown(&ctx);
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

    mel_sim_shutdown(&ctx);
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

    mel_sim_shutdown(&ctx);
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

    mel_sim_shutdown(&ctx1);
    mel_sim_shutdown(&ctx2);
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

    mel_sim_shutdown(&ctx);
}

static void counting_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    u32* count = (u32*)user;
    (*count)++;
}

static void accumulating_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    f32* total = (f32*)user;
    *total += dt;
}

MEL_TEST(sim_fixed_single_tick, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    u32 count = 0;
    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&ctx, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(fixed, counting_update, .user = &count);

    mel_sim_tick(&ctx, 1.0f / 60.0f);
    MEL_ASSERT_EQ(count, 1u);
    MEL_ASSERT_EQ(fixed->tick, (u64)1);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_fixed_multiple_ticks_per_frame, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    u32 count = 0;
    f32 fixed_dt = 1.0f / 60.0f;
    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&ctx, .fixed_dt = fixed_dt);
    mel_sim_fixed_add_update(fixed, counting_update, .user = &count);

    mel_sim_tick(&ctx, fixed_dt * 3);
    MEL_ASSERT_EQ(count, 3u);
    MEL_ASSERT_EQ(fixed->tick, (u64)3);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_fixed_accumulator_remainder, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    u32 count = 0;
    f32 fixed_dt = 1.0f / 60.0f;
    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&ctx, .fixed_dt = fixed_dt);
    mel_sim_fixed_add_update(fixed, counting_update, .user = &count);

    mel_sim_tick(&ctx, fixed_dt * 2.5f);
    MEL_ASSERT_EQ(count, 2u);
    MEL_ASSERT(fixed->accumulator > 0);
    MEL_ASSERT(fixed->accumulator < fixed_dt);
    MEL_ASSERT(fixed->alpha > 0.0f);
    MEL_ASSERT(fixed->alpha < 1.0f);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_fixed_alpha, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    f32 fixed_dt = 1.0f / 60.0f;
    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&ctx, .fixed_dt = fixed_dt);

    mel_sim_tick(&ctx, fixed_dt);
    MEL_ASSERT_FLOAT_EQ(mel_sim_fixed_alpha(fixed), 0.0f, 0.001f);

    mel_sim_tick(&ctx, fixed_dt * 0.5f);
    MEL_ASSERT_FLOAT_EQ(mel_sim_fixed_alpha(fixed), 0.5f, 0.01f);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_fixed_multiple_contexts, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    u32 fast_count = 0;
    u32 slow_count = 0;

    f32 fast_dt = 0.01f;
    f32 slow_dt = 0.1f;

    Mel_Sim_Fixed* fast = mel_sim_add_fixed(&ctx, .fixed_dt = fast_dt);
    mel_sim_fixed_add_update(fast, counting_update, .user = &fast_count);

    Mel_Sim_Fixed* slow = mel_sim_add_fixed(&ctx, .fixed_dt = slow_dt);
    mel_sim_fixed_add_update(slow, counting_update, .user = &slow_count);

    mel_sim_tick(&ctx, fast_dt * 6);
    MEL_ASSERT_EQ(fast_count, 6u);
    MEL_ASSERT_EQ(slow_count, 0u);

    mel_sim_tick(&ctx, slow_dt - fast_dt * 6);
    MEL_ASSERT_EQ(slow_count, 1u);
    MEL_ASSERT_GE(fast_count, 6u);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_fixed_multiple_updates_same_context, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    u32 count_a = 0;
    u32 count_b = 0;

    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&ctx, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(fixed, counting_update, .user = &count_a);
    mel_sim_fixed_add_update(fixed, counting_update, .user = &count_b);

    mel_sim_tick(&ctx, 1.0f / 60.0f);
    MEL_ASSERT_EQ(count_a, 1u);
    MEL_ASSERT_EQ(count_b, 1u);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_variable_update, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    f32 total_dt = 0;
    mel_sim_add_variable(&ctx, accumulating_update, .user = &total_dt);

    mel_sim_tick(&ctx, 0.016f);
    MEL_ASSERT_FLOAT_EQ(total_dt, 0.016f, 0.0001f);

    mel_sim_tick(&ctx, 0.033f);
    MEL_ASSERT_FLOAT_EQ(total_dt, 0.049f, 0.0001f);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_variable_receives_scaled_dt, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer),
        .time_scale = 0.5f);

    f32 total_dt = 0;
    mel_sim_add_variable(&ctx, accumulating_update, .user = &total_dt);

    mel_sim_tick(&ctx, 1.0f);
    MEL_ASSERT_FLOAT_EQ(total_dt, 0.5f, 0.001f);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_time_scale_affects_fixed, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer),
        .time_scale = 2.0f);

    u32 count = 0;
    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&ctx, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(fixed, counting_update, .user = &count);

    mel_sim_tick(&ctx, 1.0f / 60.0f);
    MEL_ASSERT_EQ(count, 2u);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_time_scale_zero_paused, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    u32 fixed_count = 0;
    f32 variable_dt = 0;

    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&ctx, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(fixed, counting_update, .user = &fixed_count);
    mel_sim_add_variable(&ctx, accumulating_update, .user = &variable_dt);

    ctx.time_scale = 0.0f;
    mel_sim_tick(&ctx, 1.0f);
    MEL_ASSERT_EQ(fixed_count, 0u);
    MEL_ASSERT_FLOAT_EQ(variable_dt, 0.0f, 0.0001f);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_tick_clears_events, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    Move_Event move = { .x = 1.0f, .y = 2.0f };
    mel_sim_push(&ctx, EVT_MOVE, &move, sizeof(move));

    mel_sim_tick(&ctx, 0.016f);

    Mel_Sim_Iter iter = {0};
    MEL_ASSERT_NULL(mel_sim_next(&ctx, EVT_MOVE, &iter));
    MEL_ASSERT_EQ(ctx.tick, (u64)1);

    mel_sim_shutdown(&ctx);
}

static void event_reading_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(dt);
    u32* event_count = (u32*)user;
    Mel_Sim_Iter iter = {0};
    while (mel_sim_next(sim, EVT_MOVE, &iter) != NULL)
        (*event_count)++;
}

MEL_TEST(sim_events_visible_to_all_fixed_ticks, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    u32 events_seen = 0;
    f32 fixed_dt = 1.0f / 60.0f;
    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&ctx, .fixed_dt = fixed_dt);
    mel_sim_fixed_add_update(fixed, event_reading_update, .user = &events_seen);

    Move_Event move = { .x = 1.0f, .y = 2.0f };
    mel_sim_push(&ctx, EVT_MOVE, &move, sizeof(move));

    mel_sim_tick(&ctx, fixed_dt * 3);
    MEL_ASSERT_EQ(events_seen, 3u);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_user_data, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    i32 game_state = 42;
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer),
        .user = &game_state);

    MEL_ASSERT_EQ(*(i32*)ctx.user, 42);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_remove_fixed, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    u32 count = 0;
    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&ctx, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(fixed, counting_update, .user = &count);

    mel_sim_remove_fixed(&ctx, fixed);
    MEL_ASSERT_EQ(ctx.fixed_count, 0u);

    mel_sim_tick(&ctx, 1.0f);
    MEL_ASSERT_EQ(count, 0u);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_remove_update, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    u32 count_a = 0;
    u32 count_b = 0;

    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&ctx, .fixed_dt = 1.0f / 60.0f);
    Mel_Update_Handle ha = mel_sim_fixed_add_update(fixed, counting_update, .user = &count_a);
    mel_sim_fixed_add_update(fixed, counting_update, .user = &count_b);

    mel_sim_fixed_remove_update(fixed, ha);

    mel_sim_tick(&ctx, 1.0f / 60.0f);
    MEL_ASSERT_EQ(count_a, 0u);
    MEL_ASSERT_EQ(count_b, 1u);

    mel_sim_shutdown(&ctx);
}

MEL_TEST(sim_remove_variable, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    f32 total = 0;
    Mel_Update_Handle h = mel_sim_add_variable(&ctx, accumulating_update, .user = &total);

    mel_sim_remove_variable(&ctx, h);

    mel_sim_tick(&ctx, 1.0f);
    MEL_ASSERT_FLOAT_EQ(total, 0.0f, 0.0001f);

    mel_sim_shutdown(&ctx);
}

typedef struct {
    u32 order[8];
    u32 index;
} Order_Tracker;

static void order_update_1(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    Order_Tracker* t = (Order_Tracker*)user;
    t->order[t->index++] = 1;
}

static void order_update_2(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    Order_Tracker* t = (Order_Tracker*)user;
    t->order[t->index++] = 2;
}

static void order_update_3(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    Order_Tracker* t = (Order_Tracker*)user;
    t->order[t->index++] = 3;
}

MEL_TEST(sim_execution_order, .tags = "sim")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Sim_Ctx ctx;
    mel_sim_init(&ctx, .seed = 1, .event_buffer = buffer, .event_buffer_size = sizeof(buffer));

    Order_Tracker tracker = {0};
    f32 fixed_dt = 1.0f / 60.0f;

    Mel_Sim_Fixed* fixed = mel_sim_add_fixed(&ctx, .fixed_dt = fixed_dt);
    mel_sim_fixed_add_update(fixed, order_update_1, .user = &tracker);
    mel_sim_fixed_add_update(fixed, order_update_2, .user = &tracker);
    mel_sim_add_variable(&ctx, order_update_3, .user = &tracker);

    mel_sim_tick(&ctx, fixed_dt);

    MEL_ASSERT_EQ(tracker.index, 3u);
    MEL_ASSERT_EQ(tracker.order[0], 1u);
    MEL_ASSERT_EQ(tracker.order[1], 2u);
    MEL_ASSERT_EQ(tracker.order[2], 3u);

    mel_sim_shutdown(&ctx);
}
