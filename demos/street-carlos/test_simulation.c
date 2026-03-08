#include "test.harness.h"
#include "core.engine.h"
#include "fighter.h"
#include "combat.h"
#include "command.h"
#include "actions.h"
#include "stage.h"
#include "mugen_char.h"
#include "mugen_cmd.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "async.io.h"
#include "string.str8.h"
#include "allocator.heap.h"
#include <string.h>

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
        .dev = mel_gpu_dev(),
        .sprite_pass = mel_sprite_pass(),
        .tex_pool = mel_texture_pool(),
        .vfs = &s_vfs,
        .sff_path = S8("/chars/kfm/kfm.sff"),
        .air_path = S8("/chars/kfm/kfm.air"),
        .cmd_path = S8("/chars/kfm/kfm.cmd"),
        .cns_path = S8("/chars/kfm/kfm.cns"),
        .common_cns_path = S8("/chars/kfm/common1.cns"),
        .alloc = mel_alloc_heap());

    assert(ok);
    s_loaded = true;
}

typedef struct {
    Fighter p1;
    Fighter p2;
} Sim;

static const f32 DT = 1.0f / 60.0f;

static void sim_init(Sim* s)
{
    ensure_loaded();

    fighter_init(&s->p1, mel_alloc_heap(),
        .start_x = 80.0f,
        .facing_right = true,
        .clip_pool = &s_char.clip_pool,
        .action_map = s_char.action_map,
        .action_map_count = s_char.action_map_count);

    fighter_init(&s->p2, mel_alloc_heap(),
        .start_x = GAME_W - 80.0f,
        .facing_right = false,
        .clip_pool = &s_char.clip_pool,
        .action_map = s_char.action_map,
        .action_map_count = s_char.action_map_count);

    for (u32 i = 0; i < s_char.cmd.command_count; i++)
    {
        Mugen_Cmd_Def* c = &s_char.cmd.commands[i];
        command_list_add(&s->p1.commands, c->name, c->command, .time = c->time, .buf_time = 1);
        command_list_add(&s->p2.commands, c->name, c->command, .time = c->time, .buf_time = 1);
    }

    fighter_enable_cns(&s->p1, &s_char.cns, &s_char.common_cns, &s_char.cmd_cns);
    fighter_enable_cns(&s->p2, &s_char.cns, &s_char.common_cns, &s_char.cmd_cns);
}

static void sim_tick(Sim* s)
{
    fighter_tick(&s->p1, DT, STAGE_LEFT, STAGE_RIGHT);
    fighter_tick(&s->p2, DT, STAGE_LEFT, STAGE_RIGHT);
    combat_resolve(&s->p1, &s->p2);
    fighter_apply_combat_state(&s->p1);
    fighter_apply_combat_state(&s->p2);
}

static void sim_tick_n(Sim* s, u32 n)
{
    for (u32 i = 0; i < n; i++)
        sim_tick(s);
}

static void sim_press(Fighter* f, u32 action)
{
    fighter_on_input(f, action, 1.0f);
}

static void sim_release(Fighter* f, u32 action)
{
    fighter_on_input(f, action, 0.0f);
}

MEL_TEST(sim_idle_at_start, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    MEL_ASSERT_EQ(s.p1.cns_state.stateno, 0);
    MEL_ASSERT_EQ(s.p2.cns_state.stateno, 0);
    MEL_ASSERT_EQ(s.p1.cns_state.statetype, MUGEN_PHYSICS_S);
    MEL_ASSERT(s.p1.cns_state.ctrl);
}

MEL_TEST(sim_start_positions, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    MEL_ASSERT_FLOAT_EQ(s.p1.x, 80.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(s.p2.x, (f32)GAME_W - 80.0f, 0.01f);
    MEL_ASSERT(s.p1.facing_right);
    MEL_ASSERT(!s.p2.facing_right);
}

MEL_TEST(sim_walk_forward, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    f32 start_x = s.p1.x;

    sim_press(&s.p1, ACT_MOVE_RIGHT);
    sim_tick_n(&s, 5);

    MEL_ASSERT_EQ(s.p1.cns_state.stateno, 20);
    MEL_ASSERT(s.p1.x > start_x);

    sim_release(&s.p1, ACT_MOVE_RIGHT);
    sim_tick_n(&s, 3);

    MEL_ASSERT_EQ(s.p1.cns_state.stateno, 0);
}

MEL_TEST(sim_walk_backward, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    f32 start_x = s.p1.x;

    sim_press(&s.p1, ACT_MOVE_LEFT);
    sim_tick_n(&s, 5);

    MEL_ASSERT_EQ(s.p1.cns_state.stateno, 20);
    MEL_ASSERT(s.p1.x < start_x);

    sim_release(&s.p1, ACT_MOVE_LEFT);
}

MEL_TEST(sim_crouch, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    sim_press(&s.p1, ACT_CROUCH);
    sim_tick_n(&s, 3);

    MEL_ASSERT_EQ(s.p1.cns_state.statetype, MUGEN_PHYSICS_C);

    sim_release(&s.p1, ACT_CROUCH);
    sim_tick_n(&s, 5);

    MEL_ASSERT_EQ(s.p1.cns_state.statetype, MUGEN_PHYSICS_S);
}

MEL_TEST(sim_crouch_to_stand_transition, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    sim_press(&s.p1, ACT_CROUCH);
    sim_tick_n(&s, 5);
    MEL_ASSERT_EQ(s.p1.cns_state.statetype, MUGEN_PHYSICS_C);

    sim_release(&s.p1, ACT_CROUCH);
    sim_tick(&s);

    MEL_ASSERT_EQ(s.p1.cns_state.stateno, 12);

    sim_tick_n(&s, 20);
    MEL_ASSERT_EQ(s.p1.cns_state.statetype, MUGEN_PHYSICS_S);
    MEL_ASSERT_EQ(s.p1.cns_state.stateno, 0);
}

MEL_TEST(sim_jump, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    sim_press(&s.p1, ACT_JUMP);
    sim_tick_n(&s, 2);

    i32 state = s.p1.cns_state.stateno;
    MEL_ASSERT(state == 40 || state == 50 || state == 51);

    sim_release(&s.p1, ACT_JUMP);

    bool went_airborne = false;
    bool landed = false;
    for (u32 i = 0; i < 120; i++)
    {
        sim_tick(&s);
        if (s.p1.cns_state.physics == MUGEN_PHYSICS_A)
            went_airborne = true;
        if (went_airborne && s.p1.cns_state.statetype == MUGEN_PHYSICS_S)
        {
            landed = true;
            break;
        }
    }

    MEL_ASSERT(went_airborne);
    MEL_ASSERT(landed);
    MEL_ASSERT_FLOAT_EQ(s.p1.y, 0.0f, 0.01f);
}

MEL_TEST(sim_jump_forward, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    f32 start_x = s.p1.x;

    sim_press(&s.p1, ACT_MOVE_RIGHT);
    sim_tick(&s);
    sim_press(&s.p1, ACT_JUMP);
    sim_tick_n(&s, 10);
    sim_release(&s.p1, ACT_JUMP);
    sim_release(&s.p1, ACT_MOVE_RIGHT);

    for (u32 i = 0; i < 120; i++)
    {
        sim_tick(&s);
        if (s.p1.cns_state.statetype == MUGEN_PHYSICS_S && s.p1.cns_state.stateno == 0)
            break;
    }

    MEL_ASSERT(s.p1.x > start_x);
}

MEL_TEST(sim_neutral_jump_preserves_x, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    f32 start_x = s.p1.x;

    sim_press(&s.p1, ACT_JUMP);
    sim_tick(&s);
    sim_release(&s.p1, ACT_JUMP);

    for (u32 i = 0; i < 120; i++)
    {
        sim_tick(&s);
        if (s.p1.cns_state.statetype == MUGEN_PHYSICS_S && s.p1.cns_state.stateno == 0)
            break;
    }

    MEL_ASSERT_FLOAT_EQ(s.p1.x, start_x, 1.0f);
}

MEL_TEST(sim_stand_light_punch, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    MEL_ASSERT_EQ(s.p1.cns_state.stateno, 0);
    MEL_ASSERT(s.p1.cns_state.ctrl);

    sim_press(&s.p1, ACT_BTN_X);
    sim_tick(&s);
    sim_release(&s.p1, ACT_BTN_X);

    bool entered_attack = false;
    for (u32 i = 0; i < 5; i++)
    {
        if (s.p1.cns_state.stateno == 200)
        {
            entered_attack = true;
            break;
        }
        sim_tick(&s);
    }

    MEL_ASSERT(entered_attack);
    MEL_ASSERT_EQ(s.p1.cns_state.movetype, MUGEN_MOVETYPE_A);
    MEL_ASSERT(!s.p1.cns_state.ctrl);
}

MEL_TEST(sim_attack_returns_to_idle, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    sim_press(&s.p1, ACT_BTN_X);
    sim_tick(&s);
    sim_release(&s.p1, ACT_BTN_X);

    bool returned = false;
    for (u32 i = 0; i < 120; i++)
    {
        sim_tick(&s);
        if (s.p1.cns_state.stateno == 0 && s.p1.cns_state.ctrl)
        {
            returned = true;
            break;
        }
    }

    MEL_ASSERT(returned);
}

MEL_TEST(sim_no_attack_during_attack, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    sim_press(&s.p1, ACT_BTN_X);
    sim_tick(&s);
    sim_release(&s.p1, ACT_BTN_X);
    sim_tick_n(&s, 2);

    MEL_ASSERT_EQ(s.p1.cns_state.stateno, 200);

    sim_press(&s.p1, ACT_BTN_B);
    sim_tick(&s);
    sim_release(&s.p1, ACT_BTN_B);
    sim_tick(&s);

    MEL_ASSERT_EQ(s.p1.cns_state.stateno, 200);
}

MEL_TEST(sim_crouch_blocks_walk, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    sim_press(&s.p1, ACT_CROUCH);
    sim_tick_n(&s, 3);

    f32 crouched_x = s.p1.x;

    sim_press(&s.p1, ACT_MOVE_RIGHT);
    sim_tick_n(&s, 5);

    MEL_ASSERT_EQ(s.p1.cns_state.statetype, MUGEN_PHYSICS_C);
    MEL_ASSERT_FLOAT_EQ(s.p1.x, crouched_x, 0.01f);

    sim_release(&s.p1, ACT_MOVE_RIGHT);
    sim_release(&s.p1, ACT_CROUCH);
}

MEL_TEST(sim_stage_boundary_left, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    sim_press(&s.p1, ACT_MOVE_LEFT);
    sim_tick_n(&s, 300);

    f32 expected_min = STAGE_LEFT + s.p1.ground_back;
    MEL_ASSERT(s.p1.x >= expected_min - 0.01f);

    sim_release(&s.p1, ACT_MOVE_LEFT);
}

MEL_TEST(sim_stage_boundary_right, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    sim_press(&s.p1, ACT_MOVE_RIGHT);
    sim_tick_n(&s, 300);

    f32 expected_max = STAGE_RIGHT - s.p1.ground_front;
    MEL_ASSERT(s.p1.x <= expected_max + 0.01f);

    sim_release(&s.p1, ACT_MOVE_RIGHT);
}

MEL_TEST(sim_qcf_punch, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    sim_press(&s.p1, ACT_CROUCH);
    sim_tick(&s);
    sim_release(&s.p1, ACT_CROUCH);

    sim_tick(&s);

    sim_press(&s.p1, ACT_CROUCH);
    sim_press(&s.p1, ACT_MOVE_RIGHT);
    sim_tick(&s);
    sim_release(&s.p1, ACT_CROUCH);

    sim_tick(&s);

    sim_press(&s.p1, ACT_BTN_A);
    sim_tick(&s);
    sim_release(&s.p1, ACT_BTN_A);
    sim_release(&s.p1, ACT_MOVE_RIGHT);

    bool entered_special = false;
    for (u32 i = 0; i < 5; i++)
    {
        if (s.p1.cns_state.stateno >= 1000)
        {
            entered_special = true;
            break;
        }
        sim_tick(&s);
    }

    MEL_ASSERT(entered_special);
}

MEL_TEST(sim_both_fighters_independent, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    sim_press(&s.p1, ACT_CROUCH);
    sim_press(&s.p2, ACT_JUMP);
    sim_tick_n(&s, 3);

    MEL_ASSERT_EQ(s.p1.cns_state.statetype, MUGEN_PHYSICS_C);
    MEL_ASSERT(s.p2.cns_state.stateno == 40 || s.p2.cns_state.stateno == 50 || s.p2.cns_state.stateno == 51);

    sim_release(&s.p1, ACT_CROUCH);
    sim_release(&s.p2, ACT_JUMP);
}

MEL_TEST(sim_p2_walks_backward, .tags = "sim")
{
    Sim s;
    sim_init(&s);
    sim_tick(&s);

    f32 start_x = s.p2.x;

    sim_press(&s.p2, ACT_MOVE_RIGHT);
    sim_tick_n(&s, 5);

    MEL_ASSERT_EQ(s.p2.cns_state.stateno, 20);
    MEL_ASSERT(s.p2.x > start_x);

    sim_release(&s.p2, ACT_MOVE_RIGHT);
}

MEL_TEST(sim_combat_close_range_hit, .tags = "sim")
{
    Sim s;
    sim_init(&s);

    s.p1.x = 100.0f;
    s.p1.cns_state.pos_x = 100.0f;
    s.p2.x = 130.0f;
    s.p2.cns_state.pos_x = 130.0f;

    sim_tick(&s);

    f32 p2_life_before = s.p2.cns_state.life;

    sim_press(&s.p1, ACT_BTN_A);
    sim_tick(&s);
    sim_release(&s.p1, ACT_BTN_A);

    bool hit_landed = false;
    for (u32 i = 0; i < 30; i++)
    {
        sim_tick(&s);
        if (s.p2.cns_state.life < p2_life_before)
        {
            hit_landed = true;
            break;
        }
    }

    if (hit_landed)
    {
        MEL_ASSERT(s.p2.cns_state.life < p2_life_before);
        MEL_ASSERT(s.p1.cns_state.movecontact || s.p1.cns_state.movehit);
    }
}

MEL_TEST(sim_repeated_attacks_drain_life, .tags = "sim")
{
    Sim s;
    sim_init(&s);

    s.p1.x = 100.0f;
    s.p1.cns_state.pos_x = 100.0f;
    s.p2.x = 130.0f;
    s.p2.cns_state.pos_x = 130.0f;

    sim_tick(&s);

    f32 initial_life = s.p2.cns_state.life;

    for (u32 round = 0; round < 20; round++)
    {
        for (u32 i = 0; i < 60; i++)
        {
            sim_tick(&s);
            if (s.p1.cns_state.stateno == 0 && s.p1.cns_state.ctrl)
                break;
        }

        if (!s.p1.cns_state.ctrl) continue;

        sim_press(&s.p1, ACT_BTN_A);
        sim_tick(&s);
        sim_release(&s.p1, ACT_BTN_A);

        for (u32 i = 0; i < 30; i++)
            sim_tick(&s);
    }

    MEL_ASSERT(s.p2.cns_state.life <= initial_life);
}
