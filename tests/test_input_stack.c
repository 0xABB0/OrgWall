#include "../melody/test.harness.h"
#include "../melody/input.stack.h"
#include "../melody/input.bindings.h"
#include "../melody/allocator.heap.h"

#define ACT_LEFT  1
#define ACT_RIGHT 2
#define ACT_UP    3
#define ACT_DOWN  4
#define ACT_LP    5

typedef struct {
    Mel_Input_Action last_action;
    f32 last_value;
    u32 action_count;
    bool consume;
} Test_Receiver;

static bool test_on_action(Mel_Input_Action action, f32 value, void* user)
{
    Test_Receiver* r = (Test_Receiver*)user;
    r->last_action = action;
    r->last_value = value;
    r->action_count++;
    return r->consume;
}

static SDL_Event make_key_event(SDL_Scancode scancode, bool down)
{
    SDL_Event e = {0};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = scancode;
    e.key.repeat = false;
    return e;
}

MEL_TEST(input_stack_basic_dispatch, .tags = "input")
{
    Mel_Input_Stack stack;
    mel_input_stack_init(&stack, .alloc = mel_alloc_heap());

    Mel_Input_Bindings bindings;
    Mel_Input_Binding initial[] = {
        { SDL_SCANCODE_A, ACT_LEFT },
        { SDL_SCANCODE_D, ACT_RIGHT },
    };
    mel_input_bindings_init(&bindings,
        .bindings = initial, .binding_count = 2,
        .alloc = mel_alloc_heap());

    Test_Receiver recv = {0};
    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &bindings,
        .on_action = test_on_action,
        .user = &recv);

    SDL_Event e = make_key_event(SDL_SCANCODE_A, true);
    mel_input_stack_dispatch(&stack, &e);

    MEL_ASSERT_EQ(recv.last_action, ACT_LEFT);
    MEL_ASSERT_FLOAT_EQ(recv.last_value, 1.0f, 0.001f);
    MEL_ASSERT_EQ(recv.action_count, 1);

    e = make_key_event(SDL_SCANCODE_A, false);
    mel_input_stack_dispatch(&stack, &e);

    MEL_ASSERT_EQ(recv.last_action, ACT_LEFT);
    MEL_ASSERT_FLOAT_EQ(recv.last_value, 0.0f, 0.001f);
    MEL_ASSERT_EQ(recv.action_count, 2);

    mel_input_bindings_shutdown(&bindings);
    mel_input_stack_shutdown(&stack);
}

MEL_TEST(input_stack_unbound_key_ignored, .tags = "input")
{
    Mel_Input_Stack stack;
    mel_input_stack_init(&stack, .alloc = mel_alloc_heap());

    Mel_Input_Bindings bindings;
    Mel_Input_Binding initial[] = { { SDL_SCANCODE_A, ACT_LEFT } };
    mel_input_bindings_init(&bindings,
        .bindings = initial, .binding_count = 1,
        .alloc = mel_alloc_heap());

    Test_Receiver recv = {0};
    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &bindings,
        .on_action = test_on_action,
        .user = &recv);

    SDL_Event e = make_key_event(SDL_SCANCODE_Z, true);
    mel_input_stack_dispatch(&stack, &e);

    MEL_ASSERT_EQ(recv.action_count, 0);

    mel_input_bindings_shutdown(&bindings);
    mel_input_stack_shutdown(&stack);
}

MEL_TEST(input_stack_key_repeat_ignored, .tags = "input")
{
    Mel_Input_Stack stack;
    mel_input_stack_init(&stack, .alloc = mel_alloc_heap());

    Mel_Input_Bindings bindings;
    Mel_Input_Binding initial[] = { { SDL_SCANCODE_A, ACT_LEFT } };
    mel_input_bindings_init(&bindings,
        .bindings = initial, .binding_count = 1,
        .alloc = mel_alloc_heap());

    Test_Receiver recv = {0};
    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &bindings,
        .on_action = test_on_action,
        .user = &recv);

    SDL_Event e = make_key_event(SDL_SCANCODE_A, true);
    e.key.repeat = true;
    mel_input_stack_dispatch(&stack, &e);

    MEL_ASSERT_EQ(recv.action_count, 0);

    mel_input_bindings_shutdown(&bindings);
    mel_input_stack_shutdown(&stack);
}

MEL_TEST(input_stack_passthrough, .tags = "input")
{
    Mel_Input_Stack stack;
    mel_input_stack_init(&stack, .alloc = mel_alloc_heap());

    Mel_Input_Bindings b1;
    Mel_Input_Binding init1[] = { { SDL_SCANCODE_A, ACT_LEFT } };
    mel_input_bindings_init(&b1,
        .bindings = init1, .binding_count = 1,
        .alloc = mel_alloc_heap());

    Mel_Input_Bindings b2;
    Mel_Input_Binding init2[] = { { SDL_SCANCODE_D, ACT_RIGHT } };
    mel_input_bindings_init(&b2,
        .bindings = init2, .binding_count = 1,
        .alloc = mel_alloc_heap());

    Test_Receiver r1 = {0};
    Test_Receiver r2 = {0};

    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &b1,
        .on_action = test_on_action,
        .user = &r1);

    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &b2,
        .on_action = test_on_action,
        .user = &r2);

    SDL_Event e = make_key_event(SDL_SCANCODE_A, true);
    mel_input_stack_dispatch(&stack, &e);

    MEL_ASSERT_EQ(r2.action_count, 0);
    MEL_ASSERT_EQ(r1.action_count, 1);
    MEL_ASSERT_EQ(r1.last_action, ACT_LEFT);

    mel_input_bindings_shutdown(&b1);
    mel_input_bindings_shutdown(&b2);
    mel_input_stack_shutdown(&stack);
}

MEL_TEST(input_stack_consumption_stops_propagation, .tags = "input")
{
    Mel_Input_Stack stack;
    mel_input_stack_init(&stack, .alloc = mel_alloc_heap());

    Mel_Input_Bindings b1;
    Mel_Input_Binding init1[] = { { SDL_SCANCODE_A, ACT_LEFT } };
    mel_input_bindings_init(&b1,
        .bindings = init1, .binding_count = 1,
        .alloc = mel_alloc_heap());

    Mel_Input_Bindings b2;
    Mel_Input_Binding init2[] = { { SDL_SCANCODE_A, ACT_LEFT } };
    mel_input_bindings_init(&b2,
        .bindings = init2, .binding_count = 1,
        .alloc = mel_alloc_heap());

    Test_Receiver r_bottom = {0};
    Test_Receiver r_top = { .consume = true };

    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &b1,
        .on_action = test_on_action,
        .user = &r_bottom);

    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &b2,
        .on_action = test_on_action,
        .user = &r_top);

    SDL_Event e = make_key_event(SDL_SCANCODE_A, true);
    mel_input_stack_dispatch(&stack, &e);

    MEL_ASSERT_EQ(r_top.action_count, 1);
    MEL_ASSERT_EQ(r_bottom.action_count, 0);

    mel_input_bindings_shutdown(&b1);
    mel_input_bindings_shutdown(&b2);
    mel_input_stack_shutdown(&stack);
}

MEL_TEST(input_stack_opaque_blocks_all, .tags = "input")
{
    Mel_Input_Stack stack;
    mel_input_stack_init(&stack, .alloc = mel_alloc_heap());

    Mel_Input_Bindings b_game;
    Mel_Input_Binding init_game[] = { { SDL_SCANCODE_A, ACT_LEFT } };
    mel_input_bindings_init(&b_game,
        .bindings = init_game, .binding_count = 1,
        .alloc = mel_alloc_heap());

    Mel_Input_Bindings b_pause;
    mel_input_bindings_init(&b_pause, .alloc = mel_alloc_heap());

    Test_Receiver r_game = {0};
    Test_Receiver r_pause = {0};

    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &b_game,
        .on_action = test_on_action,
        .user = &r_game);

    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &b_pause,
        .on_action = test_on_action,
        .user = &r_pause,
        .opaque = true);

    SDL_Event e = make_key_event(SDL_SCANCODE_A, true);
    mel_input_stack_dispatch(&stack, &e);

    MEL_ASSERT_EQ(r_pause.action_count, 0);
    MEL_ASSERT_EQ(r_game.action_count, 0);

    mel_input_bindings_shutdown(&b_game);
    mel_input_bindings_shutdown(&b_pause);
    mel_input_stack_shutdown(&stack);
}

MEL_TEST(input_stack_remove_layer, .tags = "input")
{
    Mel_Input_Stack stack;
    mel_input_stack_init(&stack, .alloc = mel_alloc_heap());

    Mel_Input_Bindings b1;
    Mel_Input_Binding init1[] = { { SDL_SCANCODE_A, ACT_LEFT } };
    mel_input_bindings_init(&b1,
        .bindings = init1, .binding_count = 1,
        .alloc = mel_alloc_heap());

    Mel_Input_Bindings b_blocker;
    mel_input_bindings_init(&b_blocker, .alloc = mel_alloc_heap());

    Test_Receiver r1 = {0};
    Test_Receiver r_blocker = {0};

    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &b1,
        .on_action = test_on_action,
        .user = &r1);

    Mel_Input_Layer* blocker = mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &b_blocker,
        .on_action = test_on_action,
        .user = &r_blocker,
        .opaque = true);

    SDL_Event e = make_key_event(SDL_SCANCODE_A, true);
    mel_input_stack_dispatch(&stack, &e);
    MEL_ASSERT_EQ(r1.action_count, 0);

    mel_input_stack_remove(&stack, blocker);

    mel_input_stack_dispatch(&stack, &e);
    MEL_ASSERT_EQ(r1.action_count, 1);

    mel_input_bindings_shutdown(&b1);
    mel_input_bindings_shutdown(&b_blocker);
    mel_input_stack_shutdown(&stack);
}

MEL_TEST(input_stack_siblings, .tags = "input")
{
    Mel_Input_Stack stack;
    mel_input_stack_init(&stack, .alloc = mel_alloc_heap());

    Mel_Input_Bindings b_p1;
    Mel_Input_Binding init_p1[] = {
        { SDL_SCANCODE_A, ACT_LEFT },
        { SDL_SCANCODE_D, ACT_RIGHT },
    };
    mel_input_bindings_init(&b_p1,
        .bindings = init_p1, .binding_count = 2,
        .alloc = mel_alloc_heap());

    Mel_Input_Bindings b_p2;
    Mel_Input_Binding init_p2[] = {
        { SDL_SCANCODE_LEFT, ACT_LEFT },
        { SDL_SCANCODE_RIGHT, ACT_RIGHT },
    };
    mel_input_bindings_init(&b_p2,
        .bindings = init_p2, .binding_count = 2,
        .alloc = mel_alloc_heap());

    Test_Receiver r_p1 = {0};
    Test_Receiver r_p2 = {0};

    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &b_p1,
        .on_action = test_on_action,
        .user = &r_p1);

    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &b_p2,
        .on_action = test_on_action,
        .user = &r_p2);

    SDL_Event e1 = make_key_event(SDL_SCANCODE_A, true);
    mel_input_stack_dispatch(&stack, &e1);

    SDL_Event e2 = make_key_event(SDL_SCANCODE_LEFT, true);
    mel_input_stack_dispatch(&stack, &e2);

    MEL_ASSERT_EQ(r_p1.action_count, 1);
    MEL_ASSERT_EQ(r_p1.last_action, ACT_LEFT);
    MEL_ASSERT_EQ(r_p2.action_count, 1);
    MEL_ASSERT_EQ(r_p2.last_action, ACT_LEFT);

    mel_input_bindings_shutdown(&b_p1);
    mel_input_bindings_shutdown(&b_p2);
    mel_input_stack_shutdown(&stack);
}

static Mel_Input_Map_Output custom_mapper(SDL_Event* event, void* user)
{
    MEL_UNUSED(user);
    Mel_Input_Map_Output out = {0};

    if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        out.results[out.count++] = (Mel_Input_Mapped){ .action = 100, .value = (f32)event->motion.xrel };
        out.results[out.count++] = (Mel_Input_Mapped){ .action = 101, .value = (f32)event->motion.yrel };
    }

    return out;
}

MEL_TEST(input_stack_custom_mapper, .tags = "input")
{
    Mel_Input_Stack stack;
    mel_input_stack_init(&stack, .alloc = mel_alloc_heap());

    Test_Receiver recv = {0};
    mel_input_stack_push(&stack,
        .mapper = custom_mapper,
        .on_action = test_on_action,
        .user = &recv);

    SDL_Event e = {0};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.xrel = 5.0f;
    e.motion.yrel = -3.0f;

    mel_input_stack_dispatch(&stack, &e);

    MEL_ASSERT_EQ(recv.action_count, 2);
    MEL_ASSERT_EQ(recv.last_action, 101);
    MEL_ASSERT_FLOAT_EQ(recv.last_value, -3.0f, 0.001f);

    mel_input_stack_shutdown(&stack);
}

MEL_TEST(input_bindings_runtime_rebind, .tags = "input")
{
    Mel_Input_Stack stack;
    mel_input_stack_init(&stack, .alloc = mel_alloc_heap());

    Mel_Input_Bindings bindings;
    Mel_Input_Binding initial[] = { { SDL_SCANCODE_A, ACT_LEFT } };
    mel_input_bindings_init(&bindings,
        .bindings = initial, .binding_count = 1,
        .alloc = mel_alloc_heap());

    Test_Receiver recv = {0};
    mel_input_stack_push(&stack,
        .mapper = mel_input_mapper_keyboard,
        .mapper_user = &bindings,
        .on_action = test_on_action,
        .user = &recv);

    SDL_Event e = make_key_event(SDL_SCANCODE_A, true);
    mel_input_stack_dispatch(&stack, &e);
    MEL_ASSERT_EQ(recv.last_action, ACT_LEFT);

    mel_input_bindings_remove_key(&bindings, SDL_SCANCODE_A);
    mel_input_bindings_add(&bindings, SDL_SCANCODE_Q, ACT_LEFT);

    e = make_key_event(SDL_SCANCODE_A, true);
    recv.action_count = 0;
    mel_input_stack_dispatch(&stack, &e);
    MEL_ASSERT_EQ(recv.action_count, 0);

    e = make_key_event(SDL_SCANCODE_Q, true);
    mel_input_stack_dispatch(&stack, &e);
    MEL_ASSERT_EQ(recv.action_count, 1);
    MEL_ASSERT_EQ(recv.last_action, ACT_LEFT);

    mel_input_bindings_shutdown(&bindings);
    mel_input_stack_shutdown(&stack);
}

MEL_TEST(input_bindings_get_key, .tags = "input")
{
    Mel_Input_Bindings bindings;
    Mel_Input_Binding initial[] = {
        { SDL_SCANCODE_J, ACT_LP },
        { SDL_SCANCODE_A, ACT_LEFT },
    };
    mel_input_bindings_init(&bindings,
        .bindings = initial, .binding_count = 2,
        .alloc = mel_alloc_heap());

    MEL_ASSERT_EQ(mel_input_bindings_get_key(&bindings, ACT_LP), SDL_SCANCODE_J);
    MEL_ASSERT_EQ(mel_input_bindings_get_key(&bindings, ACT_LEFT), SDL_SCANCODE_A);
    MEL_ASSERT_EQ(mel_input_bindings_get_key(&bindings, 999), SDL_SCANCODE_UNKNOWN);

    mel_input_bindings_shutdown(&bindings);
}
