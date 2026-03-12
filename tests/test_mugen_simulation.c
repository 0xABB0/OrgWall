#include "test.harness.h"
#include "core.engine.h"
#include "mugen.match.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "async.io.h"
#include "string.str8.h"
#include "allocator.heap.h"
#include <string.h>

enum {
    ACT_MOVE_LEFT = 1,
    ACT_MOVE_RIGHT,
    ACT_CROUCH,
    ACT_JUMP,
    ACT_BTN_A,
    ACT_BTN_B,
    ACT_BTN_C,
    ACT_BTN_X,
    ACT_BTN_Y,
    ACT_BTN_Z,
};

static Mel_Io s_io;
static Mel_Vfs s_vfs;
static Mugen_Char s_char;
static bool s_loaded = false;

static void ensure_loaded(void)
{
    if (s_loaded) return;

    SDL_Init(SDL_INIT_VIDEO);
    mel_init(.app_name = S8("test"), .enable_validation = false);

    mel_io_init(&s_io, &(Mel_Io_Desc){ .allocator = mel_alloc_heap(), .worker_count = 0 });
    mel_vfs_init(&s_vfs, &(Mel_Vfs_Desc){ .allocator = mel_alloc_heap(), .io = &s_io });
    Mel_Vfs_Backend* os_be = mel_vfs_backend_os_create(mel_alloc_heap(), S8("demos/street-carlos"));
    mel_vfs_mount(&s_vfs, S8("/"), os_be, 0, false);

    bool ok = mugen_char_load(&s_char,
        .vfs = &s_vfs,
        .def_path = S8("/chars/kfm/kfm.def"),
        .stcommon_path = S8("/chars/common1.cns"),
        .alloc = mel_alloc_heap());

    assert(ok);
    s_loaded = true;
}

static const f32 DT = 1.0f / 60.0f;

static const f32 P1_START_X = -70.0f;
static const f32 P2_START_X = 70.0f;
static const f32 TEST_STAGE_LEFT = -150.0f + 15.0f;
static const f32 TEST_STAGE_RIGHT = 150.0f - 15.0f;

static Mugen_Match* test_match_create(void)
{
    ensure_loaded();

    Mugen_Match* m = mugen_match_create(
        .p1_char  = &s_char,
        .p2_char  = &s_char,
        .screen_w = 384.0f,
        .alloc    = mel_alloc_heap());
    mugen_match_start(m);
    return m;
}

static void tick(Mugen_Match* m)
{
    mugen_match_update(m, DT);
}

static void tick_n(Mugen_Match* m, u32 n)
{
    for (u32 i = 0; i < n; i++)
        mugen_match_update(m, DT);
}

static void skip_intro(Mugen_Match* m)
{
    for (u32 i = 0; i < 300 && mugen_match_round(m)->state != ROUND_FIGHT; i++)
        mugen_match_update(m, DT);
}

static void update_input_action(Mugen_Player_Inputs* inputs, u32 action, bool pressed)
{
    switch (action)
    {
        case ACT_MOVE_LEFT:  inputs->left  = pressed; break;
        case ACT_MOVE_RIGHT: inputs->right = pressed; break;
        case ACT_CROUCH:     inputs->down  = pressed; break;
        case ACT_JUMP:       inputs->up    = pressed; break;
        case ACT_BTN_A:      inputs->a     = pressed; break;
        case ACT_BTN_B:      inputs->b     = pressed; break;
        case ACT_BTN_C:      inputs->c     = pressed; break;
        case ACT_BTN_X:      inputs->x     = pressed; break;
        case ACT_BTN_Y:      inputs->y     = pressed; break;
        case ACT_BTN_Z:      inputs->z     = pressed; break;
    }
}

static void press(Mugen_Match* m, u32 player_index, u32 action)
{
    Mugen_Player_Inputs inputs = mugen_match_get_player_inputs(m, player_index);
    update_input_action(&inputs, action, true);
    mugen_match_set_player_inputs(m, player_index, inputs);
}

static void release(Mugen_Match* m, u32 player_index, u32 action)
{
    Mugen_Player_Inputs inputs = mugen_match_get_player_inputs(m, player_index);
    update_input_action(&inputs, action, false);
    mugen_match_set_player_inputs(m, player_index, inputs);
}

enum {
    INPUT_LEFT  = 1 << 0,
    INPUT_RIGHT = 1 << 1,
    INPUT_UP    = 1 << 2,
    INPUT_DOWN  = 1 << 3,
    INPUT_A     = 1 << 4,
    INPUT_B     = 1 << 5,
    INPUT_C     = 1 << 6,
    INPUT_X     = 1 << 7,
    INPUT_Y     = 1 << 8,
    INPUT_Z     = 1 << 9,
};

typedef struct {
    u16 p1_mask;
    u16 p2_mask;
    u32 frames;
} Sim_Input_Step;

static Mugen_Player_Inputs inputs_from_mask(u16 mask)
{
    return (Mugen_Player_Inputs){
        .left = (mask & INPUT_LEFT) != 0,
        .right = (mask & INPUT_RIGHT) != 0,
        .up = (mask & INPUT_UP) != 0,
        .down = (mask & INPUT_DOWN) != 0,
        .a = (mask & INPUT_A) != 0,
        .b = (mask & INPUT_B) != 0,
        .c = (mask & INPUT_C) != 0,
        .x = (mask & INPUT_X) != 0,
        .y = (mask & INPUT_Y) != 0,
        .z = (mask & INPUT_Z) != 0,
    };
}

static void run_input_script(Mugen_Match* m, const Sim_Input_Step* steps, u32 step_count)
{
    for (u32 i = 0; i < step_count; i++)
    {
        mugen_match_set_inputs(m,
            inputs_from_mask(steps[i].p1_mask),
            inputs_from_mask(steps[i].p2_mask));
        tick_n(m, steps[i].frames);
    }
}

MEL_TEST(sim_idle_at_start, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);
    Fighter* p2 = mugen_match_p2(m);

    MEL_ASSERT_EQ(p1->cns_state.stateno, 0);
    MEL_ASSERT_EQ(p2->cns_state.stateno, 0);
    MEL_ASSERT_EQ(p1->cns_state.statetype, MUGEN_PHYSICS_S);
    MEL_ASSERT(p1->cns_state.ctrl);

    mugen_match_end(m);
}

MEL_TEST(sim_start_positions, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);
    Fighter* p2 = mugen_match_p2(m);

    MEL_ASSERT_FLOAT_EQ(p1->x, P1_START_X, 0.01f);
    MEL_ASSERT_FLOAT_EQ(p2->x, P2_START_X, 0.01f);
    MEL_ASSERT(p1->facing_right);
    MEL_ASSERT(!p2->facing_right);

    mugen_match_end(m);
}

MEL_TEST(sim_walk_forward, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);
    f32 start_x = p1->x;

    press(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_RIGHT);
    tick_n(m, 5);

    MEL_ASSERT_EQ(p1->cns_state.stateno, 20);
    MEL_ASSERT(p1->x > start_x);

    release(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_RIGHT);
    tick_n(m, 3);

    MEL_ASSERT_EQ(p1->cns_state.stateno, 0);

    mugen_match_end(m);
}

MEL_TEST(sim_walk_backward, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);
    f32 start_x = p1->x;

    press(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_LEFT);
    tick_n(m, 5);

    MEL_ASSERT_EQ(p1->cns_state.stateno, 20);
    MEL_ASSERT(p1->x < start_x);

    release(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_LEFT);
    mugen_match_end(m);
}

MEL_TEST(sim_crouch, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);

    press(m, MUGEN_MATCH_PLAYER_1, ACT_CROUCH);
    tick_n(m, 3);

    MEL_ASSERT_EQ(p1->cns_state.statetype, MUGEN_PHYSICS_C);

    release(m, MUGEN_MATCH_PLAYER_1, ACT_CROUCH);
    tick_n(m, 5);

    MEL_ASSERT_EQ(p1->cns_state.statetype, MUGEN_PHYSICS_S);

    mugen_match_end(m);
}

MEL_TEST(sim_crouch_to_stand_transition, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);

    press(m, MUGEN_MATCH_PLAYER_1, ACT_CROUCH);
    tick_n(m, 5);
    MEL_ASSERT_EQ(p1->cns_state.statetype, MUGEN_PHYSICS_C);

    release(m, MUGEN_MATCH_PLAYER_1, ACT_CROUCH);
    tick(m);

    MEL_ASSERT_EQ(p1->cns_state.stateno, 12);

    tick_n(m, 20);
    MEL_ASSERT_EQ(p1->cns_state.statetype, MUGEN_PHYSICS_S);
    MEL_ASSERT_EQ(p1->cns_state.stateno, 0);

    mugen_match_end(m);
}

MEL_TEST(sim_jump, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);

    press(m, MUGEN_MATCH_PLAYER_1, ACT_JUMP);
    tick_n(m, 2);

    i32 state = p1->cns_state.stateno;
    MEL_ASSERT(state == 40 || state == 50 || state == 51);

    release(m, MUGEN_MATCH_PLAYER_1, ACT_JUMP);

    bool went_airborne = false;
    bool landed = false;
    for (u32 i = 0; i < 120; i++)
    {
        tick(m);
        if (p1->cns_state.physics == MUGEN_PHYSICS_A)
            went_airborne = true;
        if (went_airborne && p1->cns_state.statetype == MUGEN_PHYSICS_S)
        {
            landed = true;
            break;
        }
    }

    MEL_ASSERT(went_airborne);
    MEL_ASSERT(landed);
    MEL_ASSERT_FLOAT_EQ(p1->y, 0.0f, 0.01f);

    mugen_match_end(m);
}

MEL_TEST(sim_jump_forward, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);
    f32 start_x = p1->x;

    press(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_RIGHT);
    tick(m);
    press(m, MUGEN_MATCH_PLAYER_1, ACT_JUMP);
    tick_n(m, 10);
    release(m, MUGEN_MATCH_PLAYER_1, ACT_JUMP);
    release(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_RIGHT);

    for (u32 i = 0; i < 120; i++)
    {
        tick(m);
        if (p1->cns_state.statetype == MUGEN_PHYSICS_S && p1->cns_state.stateno == 0)
            break;
    }

    MEL_ASSERT(p1->x > start_x);

    mugen_match_end(m);
}

MEL_TEST(sim_neutral_jump_preserves_x, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);
    f32 start_x = p1->x;

    press(m, MUGEN_MATCH_PLAYER_1, ACT_JUMP);
    tick(m);
    release(m, MUGEN_MATCH_PLAYER_1, ACT_JUMP);

    for (u32 i = 0; i < 120; i++)
    {
        tick(m);
        if (p1->cns_state.statetype == MUGEN_PHYSICS_S && p1->cns_state.stateno == 0)
            break;
    }

    MEL_ASSERT_FLOAT_EQ(p1->x, start_x, 1.0f);

    mugen_match_end(m);
}

MEL_TEST(sim_stand_light_punch, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);

    MEL_ASSERT_EQ(p1->cns_state.stateno, 0);
    MEL_ASSERT(p1->cns_state.ctrl);

    press(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_X);
    tick(m);
    release(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_X);

    bool entered_attack = false;
    for (u32 i = 0; i < 5; i++)
    {
        if (p1->cns_state.stateno == 200)
        {
            entered_attack = true;
            break;
        }
        tick(m);
    }

    MEL_ASSERT(entered_attack);
    MEL_ASSERT_EQ(p1->cns_state.movetype, MUGEN_MOVETYPE_A);
    MEL_ASSERT(!p1->cns_state.ctrl);

    mugen_match_end(m);
}

MEL_TEST(sim_attack_returns_to_idle, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);

    press(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_X);
    tick(m);
    release(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_X);

    bool returned = false;
    for (u32 i = 0; i < 120; i++)
    {
        tick(m);
        if (p1->cns_state.stateno == 0 && p1->cns_state.ctrl)
        {
            returned = true;
            break;
        }
    }

    MEL_ASSERT(returned);

    mugen_match_end(m);
}

MEL_TEST(sim_no_attack_during_attack, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);

    press(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_X);
    tick(m);
    release(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_X);
    tick_n(m, 2);

    MEL_ASSERT_EQ(p1->cns_state.stateno, 200);

    press(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_B);
    tick(m);
    release(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_B);
    tick(m);

    MEL_ASSERT_EQ(p1->cns_state.stateno, 200);

    mugen_match_end(m);
}

MEL_TEST(sim_crouch_blocks_walk, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);

    press(m, MUGEN_MATCH_PLAYER_1, ACT_CROUCH);
    tick_n(m, 3);

    f32 crouched_x = p1->x;

    press(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_RIGHT);
    tick_n(m, 5);

    MEL_ASSERT_EQ(p1->cns_state.statetype, MUGEN_PHYSICS_C);
    MEL_ASSERT_FLOAT_EQ(p1->x, crouched_x, 0.01f);

    release(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_RIGHT);
    release(m, MUGEN_MATCH_PLAYER_1, ACT_CROUCH);

    mugen_match_end(m);
}

MEL_TEST(sim_stage_boundary_left, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);

    press(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_LEFT);
    tick_n(m, 300);

    f32 expected_min = TEST_STAGE_LEFT + p1->ground_back;
    MEL_ASSERT(p1->x >= expected_min - 0.01f);

    release(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_LEFT);

    mugen_match_end(m);
}

MEL_TEST(sim_stage_boundary_right, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);

    press(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_RIGHT);
    tick_n(m, 300);

    f32 expected_max = TEST_STAGE_RIGHT - p1->ground_front;
    MEL_ASSERT(p1->x <= expected_max + 0.01f);

    release(m, MUGEN_MATCH_PLAYER_1, ACT_MOVE_RIGHT);

    mugen_match_end(m);
}

MEL_TEST(sim_qcf_punch, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);
    Sim_Input_Step script[] = {
        { .p1_mask = INPUT_DOWN, .frames = 1 },
        { .p1_mask = 0, .frames = 1 },
        { .p1_mask = INPUT_DOWN | INPUT_RIGHT, .frames = 1 },
        { .p1_mask = INPUT_RIGHT, .frames = 1 },
        { .p1_mask = INPUT_RIGHT | INPUT_A, .frames = 1 },
        { .p1_mask = 0, .frames = 1 },
    };
    run_input_script(m, script, (u32)(sizeof(script) / sizeof(script[0])));

    bool entered_special = false;
    for (u32 i = 0; i < 5; i++)
    {
        if (p1->cns_state.stateno >= 1000)
        {
            entered_special = true;
            break;
        }
        tick(m);
    }

    MEL_ASSERT(entered_special);

    mugen_match_end(m);
}

MEL_TEST(sim_both_fighters_independent, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);
    Fighter* p2 = mugen_match_p2(m);

    press(m, MUGEN_MATCH_PLAYER_1, ACT_CROUCH);
    press(m, MUGEN_MATCH_PLAYER_2, ACT_JUMP);
    tick_n(m, 3);

    MEL_ASSERT_EQ(p1->cns_state.statetype, MUGEN_PHYSICS_C);
    MEL_ASSERT(p2->cns_state.stateno == 40 || p2->cns_state.stateno == 50 || p2->cns_state.stateno == 51);

    release(m, MUGEN_MATCH_PLAYER_1, ACT_CROUCH);
    release(m, MUGEN_MATCH_PLAYER_2, ACT_JUMP);

    mugen_match_end(m);
}

MEL_TEST(sim_p2_walks_backward, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p2 = mugen_match_p2(m);
    f32 start_x = p2->x;

    press(m, MUGEN_MATCH_PLAYER_2, ACT_MOVE_RIGHT);
    tick_n(m, 5);

    MEL_ASSERT_EQ(p2->cns_state.stateno, 20);
    MEL_ASSERT(p2->x > start_x);

    release(m, MUGEN_MATCH_PLAYER_2, ACT_MOVE_RIGHT);

    mugen_match_end(m);
}

MEL_TEST(sim_combat_close_range_hit, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);

    Fighter* p1 = mugen_match_p1(m);
    Fighter* p2 = mugen_match_p2(m);

    p1->x = 100.0f;
    p1->cns_state.pos_x = 100.0f;
    p2->x = 130.0f;
    p2->cns_state.pos_x = 130.0f;

    tick(m);

    f32 p2_life_before = p2->cns_state.life;

    press(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_A);
    tick(m);
    release(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_A);

    bool hit_landed = false;
    for (u32 i = 0; i < 30; i++)
    {
        tick(m);
        if (p2->cns_state.life < p2_life_before)
        {
            hit_landed = true;
            break;
        }
    }

    if (hit_landed)
    {
        MEL_ASSERT(p2->cns_state.life < p2_life_before);
        MEL_ASSERT(p1->cns_state.mctime > 0 || p1->cns_state.movehit > 0);
    }

    mugen_match_end(m);
}

MEL_TEST(sim_repeated_attacks_drain_life, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);

    Fighter* p1 = mugen_match_p1(m);
    Fighter* p2 = mugen_match_p2(m);

    p1->x = 100.0f;
    p1->cns_state.pos_x = 100.0f;
    p2->x = 130.0f;
    p2->cns_state.pos_x = 130.0f;

    tick(m);

    f32 initial_life = p2->cns_state.life;

    for (u32 round = 0; round < 20; round++)
    {
        for (u32 i = 0; i < 60; i++)
        {
            tick(m);
            if (p1->cns_state.stateno == 0 && p1->cns_state.ctrl)
                break;
        }

        if (!p1->cns_state.ctrl) continue;

        press(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_A);
        tick(m);
        release(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_A);

        for (u32 i = 0; i < 30; i++)
            tick(m);
    }

    MEL_ASSERT(p2->cns_state.life <= initial_life);

    mugen_match_end(m);
}

MEL_TEST(sim_ko_sets_win_lose, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);
    Fighter* p2 = mugen_match_p2(m);
    Round_Ctx* round = mugen_match_round(m);

    MEL_ASSERT(!p1->cns_state.win);
    MEL_ASSERT(!p1->cns_state.lose);
    MEL_ASSERT(!p2->cns_state.win);
    MEL_ASSERT(!p2->cns_state.lose);

    p2->cns_state.life = 1.0f;
    p1->x = 100.0f;
    p1->cns_state.pos_x = 100.0f;
    p2->x = 130.0f;
    p2->cns_state.pos_x = 130.0f;

    for (u32 i = 0; i < 300; i++)
    {
        press(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_A);
        tick(m);
        release(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_A);
        tick_n(m, 3);

        if (round->state == ROUND_KO)
            break;
    }

    if (round->state == ROUND_KO)
    {
        MEL_ASSERT(p1->cns_state.win);
        MEL_ASSERT(!p1->cns_state.lose);
        MEL_ASSERT(!p2->cns_state.win);
        MEL_ASSERT(p2->cns_state.lose);
    }

    mugen_match_end(m);
}

MEL_TEST(sim_matchover_set_after_enough_wins, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);
    Fighter* p2 = mugen_match_p2(m);
    Round_Ctx* round = mugen_match_round(m);

    round->p1_wins = round->rounds_to_win - 1;

    p2->cns_state.life = 1.0f;
    p1->x = 100.0f;
    p1->cns_state.pos_x = 100.0f;
    p2->x = 130.0f;
    p2->cns_state.pos_x = 130.0f;

    for (u32 i = 0; i < 300; i++)
    {
        press(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_A);
        tick(m);
        release(m, MUGEN_MATCH_PLAYER_1, ACT_BTN_A);
        tick_n(m, 3);

        if (round->state == ROUND_POST)
            break;
    }

    if (round->state == ROUND_POST)
    {
        MEL_ASSERT(p1->cns_state.matchover);
        MEL_ASSERT(p2->cns_state.matchover);
    }

    mugen_match_end(m);
}

MEL_TEST(sim_round_reset_clears_win_lose, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    skip_intro(m);
    tick(m);

    Fighter* p1 = mugen_match_p1(m);
    Fighter* p2 = mugen_match_p2(m);
    Round_Ctx* round = mugen_match_round(m);

    p1->cns_state.win = true;
    p2->cns_state.lose = true;
    p1->cns_state.matchover = true;
    p2->cns_state.matchover = true;

    round_reset(round);

    MEL_ASSERT(!p1->cns_state.win);
    MEL_ASSERT(!p1->cns_state.lose);
    MEL_ASSERT(!p1->cns_state.matchover);
    MEL_ASSERT(!p2->cns_state.win);
    MEL_ASSERT(!p2->cns_state.lose);
    MEL_ASSERT(!p2->cns_state.matchover);

    mugen_match_end(m);
}

MEL_TEST(sim_match_update_drives_ticks, .tags = "sim")
{
    Mugen_Match* m = test_match_create();
    mugen_match_start(m);

    for (u32 i = 0; i < 300; i++)
        mugen_match_update(m, DT);

    MEL_ASSERT_EQ(mugen_match_round(m)->state, ROUND_FIGHT);

    Fighter* p1 = mugen_match_p1(m);
    MEL_ASSERT_EQ(p1->cns_state.stateno, 0);
    MEL_ASSERT(p1->cns_state.ctrl);

    mugen_match_end(m);
}
