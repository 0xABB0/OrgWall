#include "test.harness.h"
#include "command.h"
#include "string.str8.h"
#include "allocator.heap.h"

static void feed(Input_Buffer* buf, bool U, bool D, bool L, bool R, bool a)
{
    input_buffer_update(buf, U, D, L, R, a, false, false, false, false, false,
                        false, false, false, false);
}

MEL_TEST(cmd_counter_press, .tags = "command")
{
    Input_Buffer buf;
    input_buffer_init(&buf, true);

    feed(&buf, false, false, false, true, false);
    MEL_ASSERT_EQ(buf.Rb, 1);
    MEL_ASSERT_EQ(buf.Fb, 1);

    feed(&buf, false, false, false, true, false);
    MEL_ASSERT_EQ(buf.Rb, 2);
    MEL_ASSERT_EQ(buf.Fb, 2);
}

MEL_TEST(cmd_counter_release, .tags = "command")
{
    Input_Buffer buf;
    input_buffer_init(&buf, true);

    feed(&buf, false, false, false, true, false);
    feed(&buf, false, false, false, false, false);
    MEL_ASSERT_EQ(buf.Rb, -1);
    MEL_ASSERT_EQ(buf.Fb, -1);

    feed(&buf, false, false, false, false, false);
    MEL_ASSERT_EQ(buf.Rb, -2);
}

MEL_TEST(cmd_counter_prev_frame, .tags = "command")
{
    Input_Buffer buf;
    input_buffer_init(&buf, true);

    feed(&buf, false, false, false, true, false);
    feed(&buf, false, false, false, true, false);
    feed(&buf, false, false, false, true, false);
    feed(&buf, false, false, false, false, false);

    MEL_ASSERT_EQ(buf.Fp, 3);
    MEL_ASSERT_EQ(buf.Fb, -1);
}

MEL_TEST(cmd_socd_neutral, .tags = "command")
{
    Input_Buffer buf;
    input_buffer_init(&buf, true);
    buf.socd_mode = SOCD_NEUTRAL;

    feed(&buf, false, false, true, true, false);
    MEL_ASSERT(buf.Lb <= 0);
    MEL_ASSERT(buf.Rb <= 0);
}

MEL_TEST(cmd_socd_up_priority, .tags = "command")
{
    Input_Buffer buf;
    input_buffer_init(&buf, true);
    buf.socd_mode = SOCD_UP_PRIORITY;

    input_buffer_update(&buf, true, true, false, false, false, false, false,
                        false, false, false, false, false, false, false);
    MEL_ASSERT(buf.Ub > 0);
    MEL_ASSERT(buf.Db <= 0);
}

MEL_TEST(cmd_bf_derivation, .tags = "command")
{
    Input_Buffer buf_r, buf_l;
    input_buffer_init(&buf_r, true);
    input_buffer_init(&buf_l, false);

    feed(&buf_r, false, false, false, true, false);
    MEL_ASSERT_EQ(buf_r.Fb, 1);
    MEL_ASSERT(buf_r.Bb <= 0);

    feed(&buf_l, false, false, false, true, false);
    MEL_ASSERT_EQ(buf_l.Bb, 1);
    MEL_ASSERT(buf_l.Fb <= 0);
}

MEL_TEST(cmd_state_button_press, .tags = "command")
{
    Input_Buffer buf;
    input_buffer_init(&buf, true);

    feed(&buf, false, false, false, false, true);

    Command_Key key = { .key = CK_a };
    MEL_ASSERT_EQ(input_buffer_state(&buf, key), 1);

    feed(&buf, false, false, false, false, true);
    MEL_ASSERT_EQ(input_buffer_state(&buf, key), 2);
}

MEL_TEST(cmd_state_button_release, .tags = "command")
{
    Input_Buffer buf;
    input_buffer_init(&buf, true);

    feed(&buf, false, false, false, false, true);
    feed(&buf, false, false, false, false, false);

    Command_Key key = { .key = CK_a, .tilde = true };
    MEL_ASSERT_EQ(input_buffer_state(&buf, key), 1);
}

MEL_TEST(cmd_state_dir_conflict, .tags = "command")
{
    Input_Buffer buf;
    input_buffer_init(&buf, true);

    feed(&buf, false, false, false, true, false);
    feed(&buf, false, false, false, true, false);
    feed(&buf, false, false, false, true, false);

    Command_Key fwd = { .key = CK_F };
    MEL_ASSERT_EQ(input_buffer_state(&buf, fwd), 3);

    feed(&buf, false, true, false, true, false);

    Command_Key down = { .key = CK_D };
    MEL_ASSERT(input_buffer_state(&buf, down) < 0);

    feed(&buf, false, true, false, false, false);
    feed(&buf, false, true, false, false, false);

    MEL_ASSERT(input_buffer_state(&buf, down) > 0);
    MEL_ASSERT(input_buffer_state(&buf, fwd) < 0);
}

MEL_TEST(cmd_state_dollar, .tags = "command")
{
    Input_Buffer buf;
    input_buffer_init(&buf, true);

    feed(&buf, false, true, false, true, false);

    Command_Key dollar_f = { .key = CK_F, .dollar = true };
    MEL_ASSERT(input_buffer_state(&buf, dollar_f) > 0);

    Command_Key dollar_d = { .key = CK_D, .dollar = true };
    MEL_ASSERT(input_buffer_state(&buf, dollar_d) > 0);
}

MEL_TEST(cmd_charge_release_uses_prev, .tags = "command")
{
    Input_Buffer buf;
    input_buffer_init(&buf, true);

    for (i32 i = 0; i < 60; i++)
        feed(&buf, false, false, true, false, false);

    MEL_ASSERT_EQ(buf.Bb, 60);

    feed(&buf, false, false, false, false, false);

    MEL_ASSERT_EQ(buf.Bb, -1);
    MEL_ASSERT_EQ(buf.Bp, 60);

    Command_Key charge_b = { .key = CK_B, .tilde = true, .dollar = true, .chargetime = 60 };
    MEL_ASSERT_EQ(input_buffer_state_charge(&buf, charge_b), 60);
}

MEL_TEST(cmd_parser_simple_button, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("LP"), S8("a"), .time = 1, .buf_time = 1);

    MEL_ASSERT_EQ(cl.command_count, 1);
    MEL_ASSERT_EQ(cl.commands[0].step_count, 1);
    MEL_ASSERT_EQ(cl.commands[0].steps[0].key_count, 1);
    MEL_ASSERT_EQ(cl.commands[0].steps[0].keys[0].key, CK_a);

    command_list_shutdown(&cl);
}

MEL_TEST(cmd_parser_qcf, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("Hadouken"), S8("~D, DF, F, a"), .time = 15);

    MEL_ASSERT_EQ(cl.commands[0].step_count, 4);
    MEL_ASSERT(cl.commands[0].steps[0].keys[0].tilde);
    MEL_ASSERT_EQ(cl.commands[0].steps[0].keys[0].key, CK_D);
    MEL_ASSERT_EQ(cl.commands[0].steps[1].keys[0].key, CK_DF);
    MEL_ASSERT_EQ(cl.commands[0].steps[2].keys[0].key, CK_F);
    MEL_ASSERT_EQ(cl.commands[0].steps[3].keys[0].key, CK_a);

    command_list_shutdown(&cl);
}

MEL_TEST(cmd_parser_charge, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("Sonic Boom"), S8("~60$B, F, a"), .time = 10);

    Command_Key k = cl.commands[0].steps[0].keys[0];
    MEL_ASSERT(k.tilde);
    MEL_ASSERT(k.dollar);
    MEL_ASSERT_EQ(k.chargetime, 60);
    MEL_ASSERT_EQ(k.key, CK_B);

    command_list_shutdown(&cl);
}

MEL_TEST(cmd_parser_or_logic, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("Any Punch"), S8("a|b|c"), .time = 1);

    MEL_ASSERT_EQ(cl.commands[0].step_count, 1);
    MEL_ASSERT(cl.commands[0].steps[0].or_logic);
    MEL_ASSERT_EQ(cl.commands[0].steps[0].key_count, 3);

    command_list_shutdown(&cl);
}

MEL_TEST(cmd_parser_hold, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("holdfwd"), S8("/$F"), .time = 1);

    Command_Key k = cl.commands[0].steps[0].keys[0];
    MEL_ASSERT(k.slash);
    MEL_ASSERT(k.dollar);
    MEL_ASSERT_EQ(k.key, CK_F);

    command_list_shutdown(&cl);
}

MEL_TEST(cmd_fsm_single_button, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("LP"), S8("a"), .time = 1, .buf_time = 1);

    MEL_ASSERT(!command_list_active(&cl, S8("LP")));

    command_list_step(&cl, false, false, false, false,
                      true, false, false, false, false, false,
                      false, false, false, false, false, false, 0);

    MEL_ASSERT(command_list_active(&cl, S8("LP")));

    command_list_step(&cl, false, false, false, false,
                      true, false, false, false, false, false,
                      false, false, false, false, false, false, 0);

    MEL_ASSERT(!command_list_active(&cl, S8("LP")));

    command_list_shutdown(&cl);
}

MEL_TEST(cmd_fsm_qcf, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("Hadouken"), S8("~D, DF, F, a"), .time = 15, .buf_time = 1);

    command_list_step(&cl, false, true, false, false,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, false, false, false,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, true, false, true,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, false, false, true,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);

    MEL_ASSERT(!command_list_active(&cl, S8("Hadouken")));

    command_list_step(&cl, false, false, false, true,
                      true, false, false, false, false, false,
                      false, false, false, false, false, false, 0);

    MEL_ASSERT(command_list_active(&cl, S8("Hadouken")));

    command_list_shutdown(&cl);
}

MEL_TEST(cmd_fsm_no_button_no_trigger, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("Hadouken"), S8("~D, DF, F, a"), .time = 15, .buf_time = 1);

    command_list_step(&cl, false, true, false, false,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, false, false, false,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, true, false, true,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, false, false, true,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, false, false, true,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);

    MEL_ASSERT(!command_list_active(&cl, S8("Hadouken")));

    command_list_shutdown(&cl);
}

MEL_TEST(cmd_fsm_charge_move, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("Sonic Boom"), S8("~60$B, F, a"), .time = 10, .buf_time = 1);

    for (i32 i = 0; i < 60; i++)
        command_list_step(&cl, false, false, true, false,
                          false, false, false, false, false, false,
                          false, false, false, false, false, false, 0);

    command_list_step(&cl, false, false, false, true,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);

    command_list_step(&cl, false, false, false, true,
                      true, false, false, false, false, false,
                      false, false, false, false, false, false, 0);

    MEL_ASSERT(command_list_active(&cl, S8("Sonic Boom")));

    command_list_shutdown(&cl);
}

MEL_TEST(cmd_fsm_charge_too_short, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("Sonic Boom"), S8("~60$B, F, a"), .time = 10, .buf_time = 1);

    for (i32 i = 0; i < 30; i++)
        command_list_step(&cl, false, false, true, false,
                          false, false, false, false, false, false,
                          false, false, false, false, false, false, 0);

    command_list_step(&cl, false, false, false, true,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, false, false, true,
                      true, false, false, false, false, false,
                      false, false, false, false, false, false, 0);

    MEL_ASSERT(!command_list_active(&cl, S8("Sonic Boom")));

    command_list_shutdown(&cl);
}

MEL_TEST(cmd_clear_name, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("Hadouken"), S8("~D, DF, F, a"), .time = 15, .buf_time = 1);
    command_list_add(&cl, S8("Hadouken"), S8("~D, DF, F, b"), .time = 15, .buf_time = 1);

    command_list_step(&cl, false, true, false, false,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, false, false, false,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, true, false, true,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, false, false, true,
                      false, false, false, false, false, false,
                      false, false, false, false, false, false, 0);
    command_list_step(&cl, false, false, false, true,
                      true, false, false, false, false, false,
                      false, false, false, false, false, false, 0);

    MEL_ASSERT(command_list_active(&cl, S8("Hadouken")));
    MEL_ASSERT_EQ(cl.commands[1].cur_time, 0);

    command_list_shutdown(&cl);
}

MEL_TEST(cmd_autogreater, .tags = "command")
{
    Command_List cl;
    command_list_init(&cl, true, mel_alloc_heap());
    command_list_add(&cl, S8("Dash"), S8("F, F"), .time = 10, .buf_time = 1);

    MEL_ASSERT_EQ(cl.commands[0].step_count, 3);
    MEL_ASSERT_EQ(cl.commands[0].steps[0].keys[0].key, CK_F);
    MEL_ASSERT(!cl.commands[0].steps[0].keys[0].tilde);

    MEL_ASSERT_EQ(cl.commands[0].steps[1].keys[0].key, CK_F);
    MEL_ASSERT(cl.commands[0].steps[1].keys[0].tilde);
    MEL_ASSERT(cl.commands[0].steps[1].greater);

    MEL_ASSERT_EQ(cl.commands[0].steps[2].keys[0].key, CK_F);
    MEL_ASSERT(!cl.commands[0].steps[2].keys[0].tilde);
    MEL_ASSERT(cl.commands[0].steps[2].greater);

    command_list_shutdown(&cl);
}
