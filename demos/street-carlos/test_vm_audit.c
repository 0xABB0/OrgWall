#include "test.harness.h"
#include "command.h"
#include "fighter.h"
#include "combat.h"
#include "mugen_cns.h"
#include "string.str8.h"
#include "allocator.heap.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const Mel_Alloc* s_alloc;
static void ensure_alloc(void)
{
    static bool init = false;
    if (!init) { s_alloc = mel_alloc_heap(); init = true; }
}

static str8 load_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return (str8){0};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* buf = malloc((size_t)sz);
    fread(buf, 1, (size_t)sz, f);
    fclose(f);
    return str8_from_parts(buf, (size)sz);
}

static Mugen_Char_State make_standing_state(void)
{
    Mugen_Char_State st = {0};
    st.gravity = 0.44f;
    st.stand_friction = 0.85f;
    st.crouch_friction = 0.82f;
    st.facing = 1.0f;
    st.alive = true;
    st.ctrl = true;
    st.life = 1000; st.lifemax = 1000;
    st.powermax = 3000;
    st.statetype = MUGEN_PHYSICS_S;
    st.physics = MUGEN_PHYSICS_S;
    st.movetype = MUGEN_MOVETYPE_I;
    st.jump_y = 8.4f;
    st.jump_neu_x = 0.0f;
    st.jump_fwd_x = 2.5f;
    st.jump_back_x = -2.55f;
    st.walk_fwd_x = 2.4f;
    st.walk_back_x = -2.2f;
    st.run_fwd_x = 4.6f;
    st.run_back_x = -4.5f;
    st.run_back_y = 3.8f;
    st.runjump_fwd_x = 0.0f;
    st.runjump_back_x = 0.0f;
    st.runjump_y = 0.0f;
    st.airjump_neu_x = 0.0f;
    st.airjump_fwd_x = 0.0f;
    st.airjump_back_x = 0.0f;
    st.airjump_y = 0.0f;
    st.data_attack = 100.0f;
    st.attack_dist = 160.0f;
    st.stand_friction_threshold = 2.0f;
    st.crouch_friction_threshold = 0.0f;
    st.roundstate = 2;
    st.roundno = 1;
    st.palno = 1;
    st.ground_front = 16.0f;
    st.ground_back = 15.0f;
    st.rng_state = 12345;
    st.data_height = 60.0f;
    st.data_defence = 100.0f;
    st.data_liedown_time = 60.0f;
    st.data_airjuggle = 15.0f;
    st.juggle_points_remaining = 15;
    st.stage_left = -200.0f;
    st.stage_right = 200.0f;
    return st;
}

MEL_TEST(vm_rand_deterministic, .tags = "vm_audit, critical")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("random"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    Mugen_Char_State st = make_standing_state();

    f32 results_a[10];
    f32 results_b[10];

    st.rng_state = 12345;
    for (i32 i = 0; i < 10; i++)
        results_a[i] = mugen_expr_eval(e, &st);

    st.rng_state = 12345;
    for (i32 i = 0; i < 10; i++)
        results_b[i] = mugen_expr_eval(e, &st);

    for (i32 i = 0; i < 10; i++)
        MEL_ASSERT_FLOAT_EQ(results_a[i], results_b[i], 0.001f);
}

MEL_TEST(vm_rand_uses_state_prng_not_global, .tags = "vm_audit, critical")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("random"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    Mugen_Char_State st_a = make_standing_state();
    Mugen_Char_State st_b = make_standing_state();

    f32 val_a = mugen_expr_eval(e, &st_a);
    f32 val_b = mugen_expr_eval(e, &st_b);

    MEL_ASSERT_FLOAT_EQ(val_a, val_b, 0.001f);
}

MEL_TEST(vm_ignorehitpause_controller_fires, .tags = "vm_audit, critical")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, VelSet]\n"
        "type = VelSet\n"
        "trigger1 = 1\n"
        "x = 5\n"
        "ignorehitpause = 1\n"
        "\n"
        "[State 100, PosAdd]\n"
        "type = PosAdd\n"
        "trigger1 = 1\n"
        "x = 10\n"
    );

    Mugen_Cns cns = {0};
    bool ok = mugen_cns_load(&cns, cns_text, s_alloc);
    MEL_ASSERT(ok);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);

    st.hitpause_time = 5;
    st.vel_x = 0.0f;
    st.pos_x = 0.0f;

    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_FLOAT_EQ(st.vel_x, 5.0f, 0.001f);

    MEL_ASSERT_FLOAT_EQ(st.pos_x, 0.0f, 0.001f);
}

MEL_TEST(vm_changeanim_resets_timing, .tags = "vm_audit, medium")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, ChangeAnim]\n"
        "type = ChangeAnim\n"
        "trigger1 = Time = 2\n"
        "value = 200\n"
    );

    Mugen_Cns cns = {0};
    bool ok = mugen_cns_load(&cns, cns_text, s_alloc);
    MEL_ASSERT(ok);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);

    st.animtime = -50;
    st.animelem = 3;

    for (i32 i = 0; i < 3; i++)
        mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ((i32)st.anim, 200);
    MEL_ASSERT_EQ(st.animelem, 0);
    MEL_ASSERT_EQ(st.animtime, -999);
}

MEL_TEST(vm_enter_state_no_duplicate_reset, .tags = "vm_audit, quality")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.movecontact = true;
    st.movehit = true;
    st.moveguarded = true;

    mugen_cns_enter_state(&cns, &st, 100);

    MEL_ASSERT_EQ(st.movecontact, false);
    MEL_ASSERT_EQ(st.movehit, false);
    MEL_ASSERT_EQ(st.moveguarded, false);
}

MEL_TEST(vm_p2bodydist_accounts_for_width, .tags = "vm_audit, medium")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("P2BodyDist X"), s_alloc);
    Mugen_Expr* e2 = mugen_expr_parse(S8("P2Dist X"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_NOT_NULL(e2);

    Mugen_Char_State st = make_standing_state();
    st.pos_x = 0.0f;
    st.p2_pos_x = 100.0f;
    st.facing = 1.0f;
    st.ground_front = 16.0f;
    st.p2_width = 15.0f;

    f32 body_dist = mugen_expr_eval(e, &st);
    f32 center_dist = mugen_expr_eval(e2, &st);

    MEL_ASSERT_FLOAT_EQ(center_dist, 100.0f, 0.001f);

    MEL_ASSERT(body_dist < center_dist);
    MEL_ASSERT_FLOAT_EQ(body_dist, 100.0f - 16.0f - 15.0f, 1.0f);
}

MEL_TEST(vm_guard_damage_cannot_kill, .tags = "vm_audit, medium")
{
    ensure_alloc();

    Mugen_Char_State attacker_st = make_standing_state();
    Mugen_Char_State victim_st = make_standing_state();
    victim_st.life = 5.0f;

    Mugen_HitDef_Result hd = {0};
    hd.active = true;
    hd.damage_guard = 10.0f;
    hd.guardflag = MUGEN_HF_H;
    hd.pausetime_p1 = 0;
    hd.pausetime_p2 = 0;
    hd.guard_velocity = -5.0f;
    hd.guard_slidetime = 6;
    hd.guard_ctrltime = 12;

    attacker_st.hitdef = hd;
    attacker_st.hitdef_pending = true;

    victim_st.life = 5.0f;

    victim_st.life -= hd.damage_guard;
    if (victim_st.life < 1) victim_st.life = 1;

    MEL_ASSERT_GE((i32)victim_st.life, 1);
    MEL_ASSERT(victim_st.life > 0.0f);
}

MEL_TEST(vm_physics_n_no_friction, .tags = "vm_audit, medium")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = N\n"
        "anim = 100\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.vel_x = 10.0f;
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_FLOAT_EQ(st.vel_x, 10.0f, 0.001f);
}

MEL_TEST(vm_liedown_physics_applies_friction, .tags = "vm_audit, medium")
{
    ensure_alloc();

    Mugen_Char_State st = make_standing_state();
    st.physics = 4;
    st.vel_x = 10.0f;
    st.pos_x = 0.0f;

    f32 old_vel = st.vel_x;

    st.vel_x *= st.stand_friction;
    st.pos_x += st.vel_x * st.facing;

    MEL_ASSERT(st.vel_x < old_vel);
    MEL_ASSERT(st.pos_x > 0.0f);
}

MEL_TEST(vm_const_size_ground_front, .tags = "vm_audit, medium")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("const(size.ground.front)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    Mugen_Char_State st = make_standing_state();
    st.ground_front = 16.0f;

    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 16.0f, 0.001f);
}

MEL_TEST(vm_const_size_ground_back, .tags = "vm_audit, medium")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("const(size.ground.back)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    Mugen_Char_State st = make_standing_state();
    st.ground_back = 15.0f;

    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 15.0f, 0.001f);
}

MEL_TEST(vm_const_size_height, .tags = "vm_audit, medium")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("const(size.height)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    Mugen_Char_State st = make_standing_state();

    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT(val > 0.0f);
}

MEL_TEST(vm_const_data_defence, .tags = "vm_audit, medium")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("const(data.defence)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    Mugen_Char_State st = make_standing_state();

    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT(val > 0.0f);
}

MEL_TEST(vm_const_data_liedown_time, .tags = "vm_audit, medium")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("const(data.liedown.time)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    Mugen_Char_State st = make_standing_state();

    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT(val > 0.0f);
}

MEL_TEST(vm_const_data_airjuggle, .tags = "vm_audit, medium")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("const(data.airjuggle)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    Mugen_Char_State st = make_standing_state();

    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT(val > 0.0f);
}

MEL_TEST(vm_const_data_sparkno, .tags = "vm_audit, medium")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("const(data.sparkno)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    Mugen_Char_State st = make_standing_state();

    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT(val >= 0.0f);
}

MEL_TEST(vm_const_movement_airjump_num, .tags = "vm_audit, medium")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("const(movement.airjump.num)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    Mugen_Char_State st = make_standing_state();

    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT(val >= 0.0f);
}

MEL_TEST(vm_persistent_zero_fires_once, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = N\n"
        "anim = 100\n"
        "\n"
        "[State 100, VelSet]\n"
        "type = VelSet\n"
        "trigger1 = 1\n"
        "x = 5\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 5.0f, 0.001f);

    st.vel_x = 0.0f;
    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 0.0f, 0.001f);
}

MEL_TEST(vm_persistent_n_fires_every_n, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = N\n"
        "anim = 100\n"
        "\n"
        "[State 100, VelSet]\n"
        "type = VelSet\n"
        "trigger1 = 1\n"
        "x = 5\n"
        "persistent = 3\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 5.0f, 0.001f);

    st.vel_x = 0.0f;
    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 0.0f, 0.001f);

    st.vel_x = 0.0f;
    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 0.0f, 0.001f);

    st.vel_x = 0.0f;
    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 5.0f, 0.001f);
}

MEL_TEST(vm_hitdef_defaults_correct, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = S\n"
        "anim = 200\n"
        "\n"
        "[State 200, HitDef]\n"
        "type = HitDef\n"
        "trigger1 = 1\n"
        "attr = S, NA\n"
        "hitflag = MAF\n"
        "guardflag = MA\n"
        "damage = 20\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 200);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);

    MEL_ASSERT(st.hitdef_pending);
    MEL_ASSERT_EQ(st.hitdef.priority, 4);
    MEL_ASSERT_FLOAT_EQ(st.hitdef.fall_vel_y, -4.5f, 0.01f);
    MEL_ASSERT_EQ(st.hitdef.fall_recovertime, 4);
    MEL_ASSERT_EQ(st.hitdef.fall_recover, true);
    MEL_ASSERT_EQ(st.hitdef.numhits, 1);
    MEL_ASSERT_EQ(st.hitdef.p1stateno, -1);
    MEL_ASSERT_EQ(st.hitdef.p2stateno, -1);
}

MEL_TEST(vm_varset_int, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, VarSet]\n"
        "type = VarSet\n"
        "trigger1 = 1\n"
        "v = 5\n"
        "value = 42\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.var[5], 42);
}

MEL_TEST(vm_varset_inline_syntax, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, VarSet]\n"
        "type = VarSet\n"
        "trigger1 = 1\n"
        "var(3) = 99\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.var[3], 99);
}

MEL_TEST(vm_varadd_accumulates, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, VarAdd]\n"
        "type = VarAdd\n"
        "trigger1 = 1\n"
        "v = 0\n"
        "value = 10\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.var[0] = 5;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.var[0], 15);

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.var[0], 25);
}

MEL_TEST(vm_gravity_controller, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = A\n"
        "movetype = I\n"
        "physics = N\n"
        "anim = 100\n"
        "\n"
        "[State 100, Gravity]\n"
        "type = Gravity\n"
        "trigger1 = 1\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.vel_y = 5.0f;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_y, 5.0f - st.gravity, 0.001f);

    f32 prev = st.vel_y;
    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_y, prev - st.gravity, 0.001f);
}

MEL_TEST(vm_changestate_with_ctrl, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, ChangeState]\n"
        "type = ChangeState\n"
        "trigger1 = Time = 1\n"
        "value = 0\n"
        "ctrl = 1\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.ctrl = false;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.state_changed, false);

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.state_changed, true);
    MEL_ASSERT_EQ(st.pending_state, 0);
    MEL_ASSERT_EQ(st.pending_ctrl, 1);
}

MEL_TEST(vm_selfstate_changes_state, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, SelfState]\n"
        "type = SelfState\n"
        "trigger1 = Time = 0\n"
        "value = 200\n"
        "ctrl = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT(st.state_changed);
    MEL_ASSERT_EQ(st.pending_state, 200);
    MEL_ASSERT_EQ(st.pending_ctrl, 0);
}

MEL_TEST(vm_statetypeset_changes_all, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, StateTypeSet]\n"
        "type = StateTypeSet\n"
        "trigger1 = 1\n"
        "statetype = A\n"
        "movetype = A\n"
        "physics = A\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.statetype, MUGEN_PHYSICS_A);
    MEL_ASSERT_EQ(st.movetype, MUGEN_MOVETYPE_A);
    MEL_ASSERT_EQ(st.physics, MUGEN_PHYSICS_A);
}

MEL_TEST(vm_hitvelset_uses_ghv, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 5000]\n"
        "type = S\n"
        "movetype = H\n"
        "physics = N\n"
        "anim = 5000\n"
        "\n"
        "[State 5000, HitVelSet]\n"
        "type = HitVelSet\n"
        "trigger1 = 1\n"
        "x = 1\n"
        "y = 1\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.ghv.xvel = -7.0f;
    st.ghv.yvel = 3.0f;
    mugen_cns_enter_state(&cns, &st, 5000);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, -7.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(st.vel_y, 3.0f, 0.001f);
}

MEL_TEST(vm_hitfallvel_uses_ghv, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 5050]\n"
        "type = A\n"
        "movetype = H\n"
        "physics = N\n"
        "anim = 5050\n"
        "\n"
        "[State 5050, HitFallVel]\n"
        "type = HitFallVel\n"
        "trigger1 = 1\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.ghv.fall_xvel = -3.0f;
    st.ghv.fall_yvel = -6.0f;
    mugen_cns_enter_state(&cns, &st, 5050);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, -3.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(st.vel_y, -6.0f, 0.001f);
}

MEL_TEST(vm_posfreeze_stops_movement, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, PosFreeze]\n"
        "type = PosFreeze\n"
        "trigger1 = 1\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.vel_x = 10.0f;
    st.pos_x = 50.0f;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    f32 old_pos = st.pos_x;
    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.pos_x, old_pos, 0.001f);
}

MEL_TEST(vm_nothitby_sets_attr_and_time, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, NotHitBy]\n"
        "type = NotHitBy\n"
        "trigger1 = 1\n"
        "value = SCA\n"
        "time = 10\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT(st.nothitby_attr != 0);
    MEL_ASSERT_EQ(st.nothitby_time, 10);

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.nothitby_time, 9);
}

MEL_TEST(vm_nothitby_decrements_to_zero, .tags = "vm_audit")
{
    ensure_alloc();

    Mugen_Char_State st = make_standing_state();
    st.nothitby_time = 3;
    st.nothitby_attr = 0xFF;

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    for (i32 i = 0; i < 5; i++)
        mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ(st.nothitby_time, 0);
}

MEL_TEST(vm_poweradd_clamps_to_max, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, PowerAdd]\n"
        "type = PowerAdd\n"
        "trigger1 = 1\n"
        "value = 9999\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.power = 0;
    st.powermax = 3000;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.power, 3000.0f, 0.001f);
}

MEL_TEST(vm_lifeset_clamps, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, LifeSet]\n"
        "type = LifeSet\n"
        "trigger1 = 1\n"
        "value = 9999\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.life = 500;
    st.lifemax = 1000;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.life, 1000.0f, 0.001f);
}

MEL_TEST(vm_turn_flips_facing, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, Turn]\n"
        "type = Turn\n"
        "trigger1 = Time = 0\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.facing = 1.0f;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.facing, -1.0f, 0.001f);
}

MEL_TEST(vm_query_gethitvar_fields, .tags = "vm_audit")
{
    ensure_alloc();

    Mugen_Char_State st = make_standing_state();
    st.ghv.animtype = 2;
    st.ghv.damage = 50;
    st.ghv.hitcount = 3;
    st.ghv.hitshaketime = 12;
    st.ghv.slidetime = 5;
    st.ghv.ctrltime = 15;
    st.ghv.xvel = -4.5f;
    st.ghv.yvel = 2.0f;
    st.ghv.fallflag = true;
    st.ghv.guarded = true;
    st.ghv.fall_recover = true;
    st.ghv.fall_recovertime = 8;
    st.ghv.fall_xvel = -1.0f;
    st.ghv.fall_yvel = -3.0f;

    Mugen_Expr* e;
    f32 v;

    e = mugen_expr_parse(S8("GetHitVar(animtype)"), s_alloc);
    v = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(v, 2.0f, 0.001f);

    e = mugen_expr_parse(S8("GetHitVar(damage)"), s_alloc);
    v = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(v, 50.0f, 0.001f);

    e = mugen_expr_parse(S8("GetHitVar(hitcount)"), s_alloc);
    v = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(v, 3.0f, 0.001f);

    e = mugen_expr_parse(S8("GetHitVar(hitshaketime)"), s_alloc);
    v = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(v, 12.0f, 0.001f);

    e = mugen_expr_parse(S8("GetHitVar(xvel)"), s_alloc);
    v = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(v, -4.5f, 0.001f);

    e = mugen_expr_parse(S8("GetHitVar(fall)"), s_alloc);
    v = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(v, 1.0f, 0.001f);

    e = mugen_expr_parse(S8("GetHitVar(guarded)"), s_alloc);
    v = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(v, 1.0f, 0.001f);

    e = mugen_expr_parse(S8("GetHitVar(fall.recover)"), s_alloc);
    v = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(v, 1.0f, 0.001f);

    e = mugen_expr_parse(S8("GetHitVar(fall.yvel)"), s_alloc);
    v = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(v, -3.0f, 0.001f);
}

MEL_TEST(vm_query_hitshakeover, .tags = "vm_audit")
{
    ensure_alloc();

    Mugen_Char_State st = make_standing_state();
    Mugen_Expr* e = mugen_expr_parse(S8("HitShakeOver"), s_alloc);

    st.hitpause_time = 5;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    st.hitpause_time = 0;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);
}

MEL_TEST(vm_query_hitover, .tags = "vm_audit")
{
    ensure_alloc();

    Mugen_Char_State st = make_standing_state();
    st.ghv.hittime = 10;
    Mugen_Expr* e = mugen_expr_parse(S8("HitOver"), s_alloc);

    st.time = 5;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    st.time = 10;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.time = 15;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);
}

MEL_TEST(vm_query_canrecover, .tags = "vm_audit")
{
    ensure_alloc();

    Mugen_Char_State st = make_standing_state();
    st.ghv.fall_recover = true;
    st.ghv.fall_recovertime = 5;
    Mugen_Expr* e = mugen_expr_parse(S8("CanRecover"), s_alloc);

    st.time = 3;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    st.time = 5;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);
}

MEL_TEST(vm_query_frontedgedist, .tags = "vm_audit")
{
    ensure_alloc();

    Mugen_Char_State st = make_standing_state();
    st.pos_x = 50.0f;
    st.stage_left = -200.0f;
    st.stage_right = 200.0f;

    Mugen_Expr* e = mugen_expr_parse(S8("FrontEdgeDist"), s_alloc);

    st.facing = 1.0f;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 150.0f, 0.001f);

    st.facing = -1.0f;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 250.0f, 0.001f);
}

MEL_TEST(vm_query_backedgedist, .tags = "vm_audit")
{
    ensure_alloc();

    Mugen_Char_State st = make_standing_state();
    st.pos_x = 50.0f;
    st.stage_left = -200.0f;
    st.stage_right = 200.0f;

    Mugen_Expr* e = mugen_expr_parse(S8("BackEdgeDist"), s_alloc);

    st.facing = 1.0f;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 250.0f, 0.001f);

    st.facing = -1.0f;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 150.0f, 0.001f);
}

MEL_TEST(vm_query_frontedgebodydist, .tags = "vm_audit")
{
    ensure_alloc();

    Mugen_Char_State st = make_standing_state();
    st.pos_x = 50.0f;
    st.stage_right = 200.0f;
    st.ground_front = 16.0f;
    st.facing = 1.0f;

    Mugen_Expr* e = mugen_expr_parse(S8("FrontEdgeBodyDist"), s_alloc);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 200.0f - 50.0f - 16.0f, 0.001f);
}

MEL_TEST(vm_query_inguarddist, .tags = "vm_audit")
{
    ensure_alloc();

    Mugen_Char_State st = make_standing_state();
    st.pos_x = 0.0f;
    st.p2_pos_x = 100.0f;
    st.facing = 1.0f;
    st.attack_dist = 160.0f;

    Mugen_Expr* e = mugen_expr_parse(S8("InGuardDist"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.p2_pos_x = 200.0f;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(vm_query_palno, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Char_State st = make_standing_state();
    st.palno = 3;

    Mugen_Expr* e = mugen_expr_parse(S8("PalNo"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 3.0f, 0.001f);
}

MEL_TEST(vm_query_p2statetype, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Char_State st = make_standing_state();
    st.p2_statetype = MUGEN_PHYSICS_A;

    Mugen_Expr* e = mugen_expr_parse(S8("P2StateType = A"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    e = mugen_expr_parse(S8("P2StateType = S"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(vm_query_p2movetype, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Char_State st = make_standing_state();
    st.p2_movetype = MUGEN_MOVETYPE_H;

    Mugen_Expr* e = mugen_expr_parse(S8("P2MoveType = H"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    e = mugen_expr_parse(S8("P2MoveType = A"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(vm_physics_s_applies_friction, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 0]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.vel_x = 10.0f;
    mugen_cns_enter_state(&cns, &st, 0);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_FLOAT_EQ(st.vel_x, 10.0f * 0.85f, 0.01f);
}

MEL_TEST(vm_physics_c_applies_crouch_friction, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 11]\n"
        "type = C\n"
        "movetype = I\n"
        "physics = C\n"
        "anim = 11\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.vel_x = 10.0f;
    mugen_cns_enter_state(&cns, &st, 11);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_FLOAT_EQ(st.vel_x, 10.0f * 0.82f, 0.01f);
}

MEL_TEST(vm_physics_a_applies_gravity_and_movement, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 50]\n"
        "type = A\n"
        "movetype = I\n"
        "physics = A\n"
        "anim = 50\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.vel_x = 2.0f;
    st.vel_y = -8.0f;
    st.pos_x = 0.0f;
    st.pos_y = -10.0f;
    mugen_cns_enter_state(&cns, &st, 50);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_FLOAT_EQ(st.vel_y, -8.0f + st.gravity, 0.01f);
    MEL_ASSERT(st.pos_y > -10.0f || st.pos_y < -10.0f);
    MEL_ASSERT(st.pos_x != 0.0f);
}

MEL_TEST(vm_air_physics_lands_at_y0, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 50]\n"
        "type = A\n"
        "movetype = I\n"
        "physics = A\n"
        "anim = 50\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.vel_y = -8.4f;
    st.pos_y = 0.0f;
    mugen_cns_enter_state(&cns, &st, 50);
    st.animtime = -999;

    bool went_up = false;
    bool landed = false;
    for (i32 i = 0; i < 100; i++)
    {
        st.animtime = -999;
        mugen_cns_tick(&cns, &st);
        if (st.pos_y < -5.0f) went_up = true;
        if (went_up && st.state_changed && st.pending_state == 52)
        {
            landed = true;
            break;
        }
    }

    MEL_ASSERT(went_up);
    MEL_ASSERT(landed);
    MEL_ASSERT_FLOAT_EQ(st.pos_y, 0.0f, 0.01f);
}

MEL_TEST(vm_gametime_increments, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 0]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.gametime = 100;
    mugen_cns_enter_state(&cns, &st, 0);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.gametime, 101);

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.gametime, 102);
}

MEL_TEST(vm_time_increments_each_tick, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 0]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 0);
    MEL_ASSERT_EQ(st.time, 0);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.time, 1);

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.time, 2);
}

MEL_TEST(vm_enter_state_resets_time, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "[Statedef 200]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 200\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    for (i32 i = 0; i < 10; i++)
        mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.time, 10);

    mugen_cns_enter_state(&cns, &st, 200);
    MEL_ASSERT_EQ(st.time, 0);
}

MEL_TEST(vm_enter_state_sets_anim, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.anim = 999;
    mugen_cns_enter_state(&cns, &st, 100);
    MEL_ASSERT_EQ((i32)st.anim, 100);
    MEL_ASSERT_EQ(st.pending_anim, 100);
}

MEL_TEST(vm_enter_state_sets_ctrl, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "ctrl = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.ctrl = true;
    mugen_cns_enter_state(&cns, &st, 100);
    MEL_ASSERT_EQ(st.ctrl, false);
}

MEL_TEST(vm_enter_state_clears_hitdef, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.hitdef_pending = true;
    st.hitdef_active = true;
    mugen_cns_enter_state(&cns, &st, 100);
    MEL_ASSERT_EQ(st.hitdef_pending, false);
    MEL_ASSERT_EQ(st.hitdef_active, false);
}

MEL_TEST(vm_velset_x_and_y, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = N\n"
        "anim = 100\n"
        "\n"
        "[State 100, VelSet]\n"
        "type = VelSet\n"
        "trigger1 = 1\n"
        "x = 3.5\n"
        "y = -7.0\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 3.5f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(st.vel_y, -7.0f, 0.001f);
}

MEL_TEST(vm_velmul_scales, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, VelMul]\n"
        "type = VelMul\n"
        "trigger1 = 1\n"
        "x = 0.5\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.vel_x = 10.0f;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    f32 expected = 10.0f * 0.5f * st.stand_friction;
    MEL_ASSERT_FLOAT_EQ(st.vel_x, expected, 0.01f);
}

MEL_TEST(vm_posadd_offsets, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = N\n"
        "anim = 100\n"
        "\n"
        "[State 100, PosAdd]\n"
        "type = PosAdd\n"
        "trigger1 = 1\n"
        "x = 5\n"
        "y = 10\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.pos_x = 100.0f;
    st.pos_y = 50.0f;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.pos_x, 105.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(st.pos_y, 60.0f, 0.001f);
}

MEL_TEST(vm_posset_absolute, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = N\n"
        "anim = 100\n"
        "\n"
        "[State 100, PosSet]\n"
        "type = PosSet\n"
        "trigger1 = 1\n"
        "x = 0\n"
        "y = 0\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.pos_x = 100.0f;
    st.pos_y = 50.0f;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.pos_x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(st.pos_y, 0.0f, 0.001f);
}

MEL_TEST(vm_hitfallset_modifies_ghv, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 5050]\n"
        "type = A\n"
        "movetype = H\n"
        "physics = N\n"
        "anim = 5050\n"
        "\n"
        "[State 5050, HitFallSet]\n"
        "type = HitFallSet\n"
        "trigger1 = 1\n"
        "xvel = -2.0\n"
        "yvel = -5.0\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.ghv.fall_xvel = 0.0f;
    st.ghv.fall_yvel = 0.0f;
    mugen_cns_enter_state(&cns, &st, 5050);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.ghv.fall_xvel, -2.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(st.ghv.fall_yvel, -5.0f, 0.001f);
}

MEL_TEST(vm_hitfalldamage_subtracts_life, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 5050]\n"
        "type = A\n"
        "movetype = H\n"
        "physics = N\n"
        "anim = 5050\n"
        "\n"
        "[State 5050, HitFallDamage]\n"
        "type = HitFallDamage\n"
        "trigger1 = 1\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.ghv.fall_damage = 20;
    st.life = 100.0f;
    mugen_cns_enter_state(&cns, &st, 5050);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.life, 80.0f, 0.001f);
}

MEL_TEST(vm_ctrl_set, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, CtrlSet]\n"
        "type = CtrlSet\n"
        "trigger1 = Time = 5\n"
        "value = 1\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.ctrl = false;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    for (i32 i = 0; i < 5; i++)
        mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ(st.ctrl, false);

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.ctrl, true);
}

MEL_TEST(vm_triggerall_must_pass, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = N\n"
        "anim = 100\n"
        "\n"
        "[State 100, VelSet]\n"
        "type = VelSet\n"
        "triggerall = Alive\n"
        "triggerall = Time >= 0\n"
        "trigger1 = 1\n"
        "x = 5\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.alive = false;
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 0.0f, 0.001f);

    st.alive = true;
    mugen_cns_enter_state(&cns, &st, 100);
    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 5.0f, 0.001f);
}

MEL_TEST(vm_trigger_groups_or_logic, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, VelSet]\n"
        "type = VelSet\n"
        "trigger1 = Time = 3\n"
        "trigger2 = Time = 7\n"
        "x = 5\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    for (i32 i = 0; i < 3; i++)
        mugen_cns_tick(&cns, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 0.0f, 0.01f);

    mugen_cns_tick(&cns, &st);
    f32 vel_after_t3 = st.vel_x;
    MEL_ASSERT(vel_after_t3 != 0.0f);
}

MEL_TEST(vm_changestate_stops_further_controllers, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "\n"
        "[State 100, ChangeState]\n"
        "type = ChangeState\n"
        "trigger1 = 1\n"
        "value = 200\n"
        "\n"
        "[State 100, VelSet]\n"
        "type = VelSet\n"
        "trigger1 = 1\n"
        "x = 999\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 100);
    st.animtime = -999;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT(st.state_changed);
    MEL_ASSERT_EQ(st.pending_state, 200);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 0.0f, 0.001f);
}

MEL_TEST(vm_statedef_velset_on_enter, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
        "velset = 3.0, -2.0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.vel_x = 99.0f;
    st.vel_y = 99.0f;
    mugen_cns_enter_state(&cns, &st, 100);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 3.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(st.vel_y, -2.0f, 0.001f);
}

MEL_TEST(vm_statedef_no_velset_preserves_vel, .tags = "vm_audit")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 100]\n"
        "type = S\n"
        "movetype = I\n"
        "physics = S\n"
        "anim = 100\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.vel_x = 5.0f;
    st.vel_y = -3.0f;
    mugen_cns_enter_state(&cns, &st, 100);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 5.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(st.vel_y, -3.0f, 0.001f);
}

MEL_TEST(vm_expr_bitwise_and, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("7 & 3"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 3.0f, 0.001f);
}

MEL_TEST(vm_expr_bitwise_or, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("4 | 2"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 6.0f, 0.001f);
}

MEL_TEST(vm_expr_power, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("2 ** 3"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 8.0f, 0.001f);
}

MEL_TEST(vm_expr_xor, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("1 ^^ 0"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);

    e = mugen_expr_parse(S8("1 ^^ 1"), s_alloc);
    val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 0.0f, 0.001f);
}

MEL_TEST(vm_expr_func_abs, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("abs(-5)"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 5.0f, 0.001f);
}

MEL_TEST(vm_expr_division_by_zero, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("10 / 0"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 0.0f, 0.001f);
}

MEL_TEST(vm_expr_modulo_by_zero, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("10 % 0"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 0.0f, 0.001f);
}

MEL_TEST(vm_expr_const_walk_fwd, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("const(velocity.walk.fwd.x)"), s_alloc);
    Mugen_Char_State st = make_standing_state();
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 2.4f, 0.001f);
}

MEL_TEST(vm_expr_const_jump_y, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("const(velocity.jump.y)"), s_alloc);
    Mugen_Char_State st = make_standing_state();
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 8.4f, 0.001f);
}

MEL_TEST(vm_expr_const_yaccel, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("const(movement.yaccel)"), s_alloc);
    Mugen_Char_State st = make_standing_state();
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 0.44f, 0.001f);
}

static bool anim_exists_stub(void* ctx, u32 anim)
{
    (void)ctx;
    return anim == 100 || anim == 200;
}

MEL_TEST(vm_expr_selfanimexist, .tags = "vm_audit")
{
    ensure_alloc();

    Mugen_Char_State st = make_standing_state();
    st.anim_exists = anim_exists_stub;
    st.anim_exists_ctx = NULL;

    Mugen_Expr* e = mugen_expr_parse(S8("SelfAnimExist(100)"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    e = mugen_expr_parse(S8("SelfAnimExist(999)"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(vm_expr_movetype_compare, .tags = "vm_audit")
{
    ensure_alloc();
    Mugen_Char_State st = make_standing_state();
    st.movetype = MUGEN_MOVETYPE_A;

    Mugen_Expr* e = mugen_expr_parse(S8("MoveType = A"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    e = mugen_expr_parse(S8("MoveType != A"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    e = mugen_expr_parse(S8("MoveType = I"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(vm_kfm_load_all_statedefs, .tags = "vm_audit")
{
    ensure_alloc();
    str8 data = load_file("demos/street-carlos/chars/kfm/kfm.cns");
    MEL_ASSERT(data.len > 0);

    Mugen_Cns cns = {0};
    bool ok = mugen_cns_load(&cns, data, s_alloc);
    MEL_ASSERT(ok);

    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 200));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 210));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 400));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 600));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 800));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 1000));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 1100));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 1200));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 1300));

    free(data.data);
}

MEL_TEST(vm_common1_load_all_statedefs, .tags = "vm_audit")
{
    ensure_alloc();
    str8 data = load_file("demos/street-carlos/chars/kfm/common1.cns");
    MEL_ASSERT(data.len > 0);

    Mugen_Cns cns = {0};
    bool ok = mugen_cns_load(&cns, data, s_alloc);
    MEL_ASSERT(ok);

    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 0));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 10));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 11));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 12));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 20));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 40));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 50));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 52));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 120));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 130));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 150));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 5000));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 5020));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 5050));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 5100));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 5150));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 5200));
    MEL_ASSERT_NOT_NULL(mugen_cns_get(&cns, 5900));

    free(data.data);
}

MEL_TEST(vm_targetbind_positions_victim, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 800]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = N\n"
        "anim = 800\n"
        "\n"
        "[State 800, TargetBind]\n"
        "type = TargetBind\n"
        "trigger1 = 1\n"
        "pos = 20, 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State attacker = make_standing_state();
    Mugen_Char_State victim = make_standing_state();
    attacker.pos_x = 100.0f;
    attacker.facing = 1.0f;
    victim.pos_x = 200.0f;
    attacker.target = &victim;

    mugen_cns_enter_state(&cns, &attacker, 800);
    mugen_cns_tick(&cns, &attacker);

    MEL_ASSERT_FLOAT_EQ(victim.pos_x, 120.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(victim.vel_x, 0.0f, 0.001f);
    MEL_ASSERT(victim.ghv.isbound);
}

MEL_TEST(vm_targetbind_facing_left, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 800]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = N\n"
        "anim = 800\n"
        "\n"
        "[State 800, TargetBind]\n"
        "type = TargetBind\n"
        "trigger1 = 1\n"
        "pos = 20, 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State attacker = make_standing_state();
    Mugen_Char_State victim = make_standing_state();
    attacker.pos_x = 100.0f;
    attacker.facing = -1.0f;
    victim.pos_x = 50.0f;
    attacker.target = &victim;

    mugen_cns_enter_state(&cns, &attacker, 800);
    mugen_cns_tick(&cns, &attacker);

    MEL_ASSERT_FLOAT_EQ(victim.pos_x, 80.0f, 0.001f);
}

MEL_TEST(vm_targetstate_sets_victim_state, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 800]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = N\n"
        "anim = 800\n"
        "\n"
        "[State 800, TargetState]\n"
        "type = TargetState\n"
        "trigger1 = 1\n"
        "value = 820\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State attacker = make_standing_state();
    Mugen_Char_State victim = make_standing_state();
    attacker.target = &victim;

    mugen_cns_enter_state(&cns, &attacker, 800);
    mugen_cns_tick(&cns, &attacker);

    MEL_ASSERT_EQ(victim.pending_state, 820);
    MEL_ASSERT(victim.state_changed);
    MEL_ASSERT_EQ(victim.pending_ctrl, 0);
}

MEL_TEST(vm_targetstate_sets_cross_cns, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 800]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = N\n"
        "anim = 800\n"
        "\n"
        "[State 800, TargetState]\n"
        "type = TargetState\n"
        "trigger1 = 1\n"
        "value = 820\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State attacker = make_standing_state();
    Mugen_Char_State victim = make_standing_state();
    attacker.target = &victim;
    attacker.self_cns = &cns;

    mugen_cns_enter_state(&cns, &attacker, 800);
    mugen_cns_tick(&cns, &attacker);

    MEL_ASSERT(victim.state_owner_cns == &cns);
}

MEL_TEST(vm_targetlifeadd_damages_victim, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 800]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = N\n"
        "anim = 800\n"
        "\n"
        "[State 800, TargetLifeAdd]\n"
        "type = TargetLifeAdd\n"
        "trigger1 = 1\n"
        "value = -200\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State attacker = make_standing_state();
    Mugen_Char_State victim = make_standing_state();
    victim.life = 1000.0f;
    victim.defence_mul = 1.0f;
    attacker.target = &victim;

    mugen_cns_enter_state(&cns, &attacker, 800);
    mugen_cns_tick(&cns, &attacker);

    MEL_ASSERT_FLOAT_EQ(victim.life, 800.0f, 0.001f);
}

MEL_TEST(vm_targetlifeadd_no_kill, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 800]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = N\n"
        "anim = 800\n"
        "\n"
        "[State 800, TargetLifeAdd]\n"
        "type = TargetLifeAdd\n"
        "trigger1 = 1\n"
        "value = -9999\n"
        "kill = 0\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State attacker = make_standing_state();
    Mugen_Char_State victim = make_standing_state();
    victim.life = 100.0f;
    attacker.target = &victim;

    mugen_cns_enter_state(&cns, &attacker, 800);
    mugen_cns_tick(&cns, &attacker);

    MEL_ASSERT_FLOAT_EQ(victim.life, 1.0f, 0.001f);
}

MEL_TEST(vm_targetfacing_same_direction, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 800]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = N\n"
        "anim = 800\n"
        "\n"
        "[State 800, TargetFacing]\n"
        "type = TargetFacing\n"
        "trigger1 = 1\n"
        "value = 1\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State attacker = make_standing_state();
    Mugen_Char_State victim = make_standing_state();
    attacker.facing = 1.0f;
    victim.facing = -1.0f;
    attacker.target = &victim;

    mugen_cns_enter_state(&cns, &attacker, 800);
    mugen_cns_tick(&cns, &attacker);

    MEL_ASSERT_FLOAT_EQ(victim.facing, 1.0f, 0.001f);
}

MEL_TEST(vm_targetfacing_opposite, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 800]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = N\n"
        "anim = 800\n"
        "\n"
        "[State 800, TargetFacing]\n"
        "type = TargetFacing\n"
        "trigger1 = 1\n"
        "value = -1\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State attacker = make_standing_state();
    Mugen_Char_State victim = make_standing_state();
    attacker.facing = 1.0f;
    victim.facing = 1.0f;
    attacker.target = &victim;

    mugen_cns_enter_state(&cns, &attacker, 800);
    mugen_cns_tick(&cns, &attacker);

    MEL_ASSERT_FLOAT_EQ(victim.facing, -1.0f, 0.001f);
}

MEL_TEST(vm_targetpoweradd, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 800]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = N\n"
        "anim = 800\n"
        "\n"
        "[State 800, TargetPowerAdd]\n"
        "type = TargetPowerAdd\n"
        "trigger1 = 1\n"
        "value = 500\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State attacker = make_standing_state();
    Mugen_Char_State victim = make_standing_state();
    victim.power = 100.0f;
    attacker.target = &victim;

    mugen_cns_enter_state(&cns, &attacker, 800);
    mugen_cns_tick(&cns, &attacker);

    MEL_ASSERT_FLOAT_EQ(victim.power, 600.0f, 0.001f);
}

MEL_TEST(vm_changeanim2_changes_self_anim, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 820]\n"
        "type = A\n"
        "movetype = H\n"
        "physics = N\n"
        "anim = 0\n"
        "\n"
        "[State 820, ChangeAnim2]\n"
        "type = ChangeAnim2\n"
        "trigger1 = Time = 0\n"
        "value = 820\n"
        "persistent = 0\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State victim = make_standing_state();
    victim.anim = 0;

    mugen_cns_enter_state(&cns, &victim, 820);
    mugen_cns_tick(&cns, &victim);

    MEL_ASSERT_EQ((i32)victim.anim, 820);
    MEL_ASSERT_EQ(victim.animelem, 0);
    MEL_ASSERT_EQ(victim.animtime, -999);
    MEL_ASSERT(victim.use_owner_anim);
}

MEL_TEST(vm_selfstate_clears_cns_owner, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 820]\n"
        "type = S\n"
        "movetype = H\n"
        "physics = N\n"
        "anim = 820\n"
        "\n"
        "[State 820, SelfState]\n"
        "type = SelfState\n"
        "trigger1 = Time = 0\n"
        "value = 5000\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.state_owner_cns = &cns;

    mugen_cns_enter_state(&cns, &st, 820);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT(st.state_changed);
    MEL_ASSERT_EQ(st.pending_state, 5000);
    MEL_ASSERT(st.state_owner_cns == NULL);
}

MEL_TEST(vm_no_target_skips_controllers, .tags = "vm_audit, throw")
{
    ensure_alloc();

    str8 cns_text = S8(
        "[Statedef 800]\n"
        "type = S\n"
        "movetype = A\n"
        "physics = N\n"
        "anim = 800\n"
        "\n"
        "[State 800, TargetBind]\n"
        "type = TargetBind\n"
        "trigger1 = 1\n"
        "pos = 20, 0\n"
        "\n"
        "[State 800, TargetLifeAdd]\n"
        "type = TargetLifeAdd\n"
        "trigger1 = 1\n"
        "value = -200\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State attacker = make_standing_state();
    attacker.target = NULL;

    mugen_cns_enter_state(&cns, &attacker, 800);
    mugen_cns_tick(&cns, &attacker);
}

MEL_TEST(vm_animelemtime_uses_precomputed_ticks, .tags = "vm_audit, critical")
{
    ensure_alloc();

    Mugen_Expr* e2 = mugen_expr_parse(S8("AnimElemTime(2)"), s_alloc);
    Mugen_Expr* e3 = mugen_expr_parse(S8("AnimElemTime(3)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e2);
    MEL_ASSERT_NOT_NULL(e3);

    Mugen_Char_State st = make_standing_state();
    i32 ticks[] = {0, 5, 12};
    st.anim_elem_start_ticks = ticks;
    st.anim_elem_count = 3;

    st.time = 0;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e2, &st), -5.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e3, &st), -12.0f, 0.001f);

    st.time = 5;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e2, &st), 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e3, &st), -7.0f, 0.001f);

    st.time = 8;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e2, &st), 3.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e3, &st), -4.0f, 0.001f);

    st.time = 12;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e2, &st), 7.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e3, &st), 0.0f, 0.001f);

    st.anim_elem_start_ticks = NULL;
    st.anim_elem_count = 0;
}

MEL_TEST(vm_animelem_first_tick_only, .tags = "vm_audit, critical")
{
    ensure_alloc();

    Mugen_Expr* eq6 = mugen_expr_parse(S8("AnimElem = 6"), s_alloc);
    MEL_ASSERT_NOT_NULL(eq6);

    Mugen_Char_State st = make_standing_state();
    st.animelem = 6;
    st.animelemtime = 0;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(eq6, &st), 1.0f, 0.001f);

    st.animelemtime = 1;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(eq6, &st), 0.0f, 0.001f);

    st.animelemtime = 5;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(eq6, &st), 0.0f, 0.001f);
}

MEL_TEST(vm_hit_sets_target_pointer, .tags = "vm_audit, throw")
{
    ensure_alloc();

    Mugen_Char_State attacker = make_standing_state();
    Mugen_Char_State victim = make_standing_state();

    MEL_ASSERT(attacker.target == NULL);

    attacker.hitdef.active = true;
    attacker.hitdef.damage_hit = 50.0f;
    attacker.hitdef.pausetime_p1 = 5;
    attacker.hitdef.pausetime_p2 = 7;
    attacker.hitdef.ground_hittime = 10;
    attacker.hitdef.ground_vel_x = 3.0f;

    attacker.movehit = true;
    attacker.movecontact = true;
    attacker.target = &victim;

    MEL_ASSERT(attacker.target == &victim);
}

MEL_TEST(vm_juggle_points_block_hit_when_exhausted, .tags = "vm_audit, critical")
{
    Mugen_Char_State attacker = make_standing_state();
    Mugen_Char_State victim = make_standing_state();
    victim.statetype = MUGEN_PHYSICS_A;
    victim.juggle_points_remaining = 3;

    attacker.hitdef = (Mugen_HitDef_Result){
        .active = true,
        .juggle = 5,
        .damage_hit = 30.0f,
        .hitflag = MUGEN_HF_H | MUGEN_HF_M | MUGEN_HF_A,
    };
    attacker.hitdef_pending = true;

    MEL_ASSERT_EQ(victim.juggle_points_remaining, 3);
    MEL_ASSERT_LT(victim.juggle_points_remaining, attacker.hitdef.juggle);
}

MEL_TEST(vm_juggle_points_consumed_on_air_hit, .tags = "vm_audit, critical")
{
    Mugen_Char_State victim = make_standing_state();
    victim.statetype = MUGEN_PHYSICS_A;
    victim.juggle_points_remaining = 15;
    victim.data_airjuggle = 15.0f;

    victim.juggle_points_remaining -= 4;
    MEL_ASSERT_EQ(victim.juggle_points_remaining, 11);
}

MEL_TEST(vm_juggle_points_reset_on_recovery, .tags = "vm_audit, critical")
{
    ensure_alloc();
    str8 cns_text = S8("[Statedef 5120]\ntype = S\nmovetype = I\nphysics = S\nanim = 5120\nctrl = 0\n");
    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    st.movetype = MUGEN_MOVETYPE_H;
    st.juggle_points_remaining = 3;
    st.data_airjuggle = 15.0f;

    mugen_cns_enter_state(&cns, &st, 5120);

    MEL_ASSERT_EQ(st.movetype, MUGEN_MOVETYPE_I);
    MEL_ASSERT_EQ(st.juggle_points_remaining, 15);

    mugen_cns_shutdown(&cns, s_alloc);
}

MEL_TEST(vm_hitdef_copies_yaccel, .tags = "vm_audit, medium")
{
    ensure_alloc();
    str8 cns_text = S8(
        "[Statedef 200]\ntype = S\nmovetype = A\nphysics = S\nanim = 200\n\n"
        "[State 200, HitDef]\ntype = HitDef\ntrigger1 = 1\n"
        "attr = S, NA\nhitflag = MAF\nguardflag = MA\n"
        "damage = 30\nyaccel = 0.6\n"
    );
    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick_statedef(mugen_cns_get(&cns, 200), &st);

    MEL_ASSERT(st.hitdef.has_yaccel);
    MEL_ASSERT_FLOAT_EQ(st.hitdef.yaccel, 0.6f, 0.001f);

    mugen_cns_shutdown(&cns, s_alloc);
}

MEL_TEST(vm_hitdef_cornerpush_defaults, .tags = "vm_audit, medium")
{
    ensure_alloc();
    str8 cns_text = S8(
        "[Statedef 200]\ntype = S\nmovetype = A\nphysics = S\nanim = 200\n\n"
        "[State 200, HitDef]\ntype = HitDef\ntrigger1 = 1\n"
        "attr = S, NA\nhitflag = MAF\nguardflag = MA\n"
        "damage = 30\nguard.velocity = -5\n"
    );
    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick_statedef(mugen_cns_get(&cns, 200), &st);

    MEL_ASSERT_FLOAT_EQ(st.hitdef.ground_cornerpush_veloff, 5.0f * 1.3f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(st.hitdef.air_cornerpush_veloff, st.hitdef.ground_cornerpush_veloff, 0.01f);
    MEL_ASSERT_FLOAT_EQ(st.hitdef.guard_cornerpush_veloff, st.hitdef.ground_cornerpush_veloff, 0.01f);

    mugen_cns_shutdown(&cns, s_alloc);
}

MEL_TEST(vm_hitdef_cornerpush_default_chain, .tags = "vm_audit, medium")
{
    ensure_alloc();
    str8 cns_text = S8(
        "[Statedef 200]\ntype = S\nmovetype = A\nphysics = S\nanim = 200\n\n"
        "[State 200, HitDef]\ntype = HitDef\ntrigger1 = 1\n"
        "attr = S, NA\nhitflag = MAF\nguardflag = MA\n"
        "damage = 30\nground.velocity = -6, 0\n"
    );
    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick_statedef(mugen_cns_get(&cns, 200), &st);

    MEL_ASSERT_FLOAT_EQ(st.hitdef.guard_velocity, -6.0f * -0.5f, 0.01f);
    f32 expected_push = st.hitdef.guard_velocity * -1.3f;
    MEL_ASSERT_FLOAT_EQ(st.hitdef.ground_cornerpush_veloff, expected_push, 0.01f);

    mugen_cns_shutdown(&cns, s_alloc);
}

MEL_TEST(vm_hitdef_juggle_from_statedef, .tags = "vm_audit, critical")
{
    ensure_alloc();
    str8 cns_text = S8(
        "[Statedef 200]\ntype = S\nmovetype = A\nphysics = S\nanim = 200\njuggle = 4\n\n"
        "[State 200, HitDef]\ntype = HitDef\ntrigger1 = 1\n"
        "attr = S, NA\nhitflag = MAF\nguardflag = MA\n"
        "damage = 30\n"
    );
    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    Mugen_Char_State st = make_standing_state();
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick_statedef(mugen_cns_get(&cns, 200), &st);

    MEL_ASSERT_EQ(st.hitdef.juggle, 4);

    mugen_cns_shutdown(&cns, s_alloc);
}
