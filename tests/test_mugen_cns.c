#include "test.harness.h"
#include "mugen.cns.h"
#include "mugen.air.h"
#include "string.str8.h"
#include "allocator.h"
#include "allocator.heap.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if 0

static const Mel_Alloc* s_alloc;
static void ensure_alloc(void)
{
    static bool init = false;
    if (!init) { s_alloc = mel_alloc_heap(); init = true; }
}

MEL_TEST(cns_expr_literal_int, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("42"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_EQ(e->type, MUGEN_EXPR_LIT_INT);
    MEL_ASSERT_EQ(e->lit_int, 42);
}

MEL_TEST(cns_expr_literal_float, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("3.14"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_EQ(e->type, MUGEN_EXPR_LIT_FLOAT);
    MEL_ASSERT_FLOAT_EQ(e->lit_float, 3.14f, 0.01f);
}

MEL_TEST(cns_expr_negative, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("-5"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, -5.0f, 0.001f);
}

MEL_TEST(cns_expr_arithmetic, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("2 + 3 * 4"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 14.0f, 0.001f);
}

MEL_TEST(cns_expr_parens, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("(2 + 3) * 4"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 20.0f, 0.001f);
}

MEL_TEST(cns_expr_comparison, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("5 > 3"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);

    e = mugen_expr_parse(S8("5 < 3"), s_alloc);
    val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 0.0f, 0.001f);
}

MEL_TEST(cns_expr_equality, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("5 = 5"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);

    e = mugen_expr_parse(S8("5 != 3"), s_alloc);
    val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);
}

MEL_TEST(cns_expr_logic, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("1 && 0"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 0.0f, 0.001f);

    e = mugen_expr_parse(S8("1 || 0"), s_alloc);
    val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);
}

MEL_TEST(cns_expr_not, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("!0"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);

    e = mugen_expr_parse(S8("!1"), s_alloc);
    val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 0.0f, 0.001f);
}

MEL_TEST(cns_expr_func_ceil, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("ceil(3.2)"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 4.0f, 0.001f);
}

MEL_TEST(cns_expr_func_floor, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("floor(3.7)"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 3.0f, 0.001f);
}

MEL_TEST(cns_expr_func_ifelse, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("ifelse(1, 10, 20)"), s_alloc);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 10.0f, 0.001f);

    e = mugen_expr_parse(S8("ifelse(0, 10, 20)"), s_alloc);
    val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 20.0f, 0.001f);
}

MEL_TEST(cns_expr_query_time, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.time = 7;

    Mugen_Expr* e = mugen_expr_parse(S8("Time = 7"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);

    e = mugen_expr_parse(S8("Time = 3"), s_alloc);
    val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 0.0f, 0.001f);
}

MEL_TEST(cns_expr_query_vel, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.vel_y = 5.0f;

    Mugen_Expr* e = mugen_expr_parse(S8("Vel Y > 0"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);
}

MEL_TEST(cns_expr_query_pos, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.pos_y = 0.0f;

    Mugen_Expr* e = mugen_expr_parse(S8("Pos Y >= 0"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);
}

MEL_TEST(cns_expr_query_statetype, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.statetype = MUGEN_PHYSICS_S;

    Mugen_Expr* e = mugen_expr_parse(S8("statetype = S"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);

    e = mugen_expr_parse(S8("statetype != A"), s_alloc);
    val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);

    e = mugen_expr_parse(S8("statetype = A"), s_alloc);
    val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 0.0f, 0.001f);
}

MEL_TEST(cns_expr_var, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.fvar[11] = 2.0f;

    Mugen_Expr* e = mugen_expr_parse(S8("fvar(11)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 2.0f, 0.001f);
}

MEL_TEST(cns_expr_complex_damage, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.fvar[11] = 2.0f;

    Mugen_Expr* e = mugen_expr_parse(S8("ceil(22*(1-0.45*(fvar(11)=2)))"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    f32 expected = ceilf(22.0f * (1.0f - 0.45f * 1.0f));
    MEL_ASSERT_FLOAT_EQ(val, expected, 0.001f);
}

MEL_TEST(cns_expr_modulo, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.prevstateno = 1092;

    Mugen_Expr* e = mugen_expr_parse(S8("8+2*(prevstateno%10)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 12.0f, 0.001f);
}

MEL_TEST(cns_expr_movecontact_negated, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.mctime = 0;

    Mugen_Expr* e = mugen_expr_parse(S8("!movecontact"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);

    st.mctime = 1;
    val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 0.0f, 0.001f);
}

MEL_TEST(cns_expr_ctrl, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.ctrl = true;

    Mugen_Expr* e = mugen_expr_parse(S8("ctrl"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);
}

MEL_TEST(cns_expr_animtime, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.animtime = 0;

    Mugen_Expr* e = mugen_expr_parse(S8("AnimTime = 0"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 1.0f, 0.001f);
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

MEL_TEST(cns_load_poison, .tags = "cns")
{
    ensure_alloc();
    str8 data = load_file("demos/street-carlos/chars/poi-son/poi-son.cns");
    MEL_ASSERT(data.len > 0);

    Mugen_Cns cns = {0};
    bool ok = mugen_cns_load(&cns, data, s_alloc);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(cns.statedef_count, 43u);

    Mugen_Statedef* s421 = mugen_cns_get(&cns, 421);
    MEL_ASSERT_NOT_NULL(s421);
    MEL_ASSERT_EQ(s421->statetype, MUGEN_PHYSICS_C);
    MEL_ASSERT_EQ(s421->movetype, MUGEN_MOVETYPE_A);
    MEL_ASSERT_EQ(s421->physics, MUGEN_PHYSICS_N);
    MEL_ASSERT_EQ(s421->anim, 420);
    MEL_ASSERT_EQ(s421->ctrl, 0);
    MEL_ASSERT_EQ(s421->juggle, 8);

    MEL_ASSERT_GE(s421->controller_count, 4u);

    Mugen_State_Controller* sc0 = &s421->controllers[0];
    MEL_ASSERT_EQ(sc0->type, MUGEN_SC_PLAYSND);

    Mugen_State_Controller* sc1 = &s421->controllers[1];
    MEL_ASSERT_EQ(sc1->type, MUGEN_SC_HITDEF);
    MEL_ASSERT_NOT_NULL(sc1->params);
    {
        Mugen_Char_State tmp = {0};
        tmp.facing = 1;
        Mugen_SC_Reg* reg = mugen_sc_get_reg(MUGEN_SC_HITDEF);
        reg->exec(sc1, &tmp);
        MEL_ASSERT(tmp.hitdef.attr & MUGEN_ATTR_C);
        MEL_ASSERT(tmp.hitdef.attr & MUGEN_ATTR_NA);
        MEL_ASSERT_EQ(tmp.hitdef.ground_type, MUGEN_GROUNDTYPE_TRIP);
        MEL_ASSERT(tmp.hitdef_pending);
    }

    Mugen_State_Controller* sc2 = &s421->controllers[2];
    MEL_ASSERT_EQ(sc2->type, MUGEN_SC_VELSET);
    MEL_ASSERT_NOT_NULL(sc2->params);

    Mugen_State_Controller* sc3 = &s421->controllers[3];
    MEL_ASSERT_EQ(sc3->type, MUGEN_SC_VELSET);

    Mugen_State_Controller* sc4 = &s421->controllers[4];
    MEL_ASSERT_EQ(sc4->type, MUGEN_SC_CHANGESTATE);
    MEL_ASSERT_NOT_NULL(sc4->params);

    free(data.data);
}

MEL_TEST(cns_load_common, .tags = "cns")
{
    ensure_alloc();
    str8 data = load_file("demos/street-carlos/chars/poi-son/common-one.cns");
    MEL_ASSERT(data.len > 0);

    Mugen_Cns cns = {0};
    bool ok = mugen_cns_load(&cns, data, s_alloc);
    MEL_ASSERT(ok);
    MEL_ASSERT_EQ(cns.statedef_count, 17u);

    Mugen_Statedef* s0 = mugen_cns_get(&cns, 0);
    MEL_ASSERT_NOT_NULL(s0);

    Mugen_Statedef* s40 = mugen_cns_get(&cns, 40);
    MEL_ASSERT_NOT_NULL(s40);

    Mugen_Statedef* s5000 = mugen_cns_get(&cns, 5000);
    MEL_ASSERT_NOT_NULL(s5000);

    free(data.data);
}

MEL_TEST(cns_eval_state421_velset, .tags = "cns")
{
    ensure_alloc();
    str8 data = load_file("demos/street-carlos/chars/poi-son/poi-son.cns");
    MEL_ASSERT(data.len > 0);

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, data, s_alloc);

    Mugen_Statedef* s421 = mugen_cns_get(&cns, 421);
    MEL_ASSERT_NOT_NULL(s421);

    Mugen_State_Controller* velset1 = &s421->controllers[2];
    MEL_ASSERT_EQ(velset1->type, MUGEN_SC_VELSET);

    MEL_ASSERT_EQ(velset1->trigger_group_count, 1u);
    MEL_ASSERT_GT(velset1->trigger_groups[0].count, 0u);

    Mugen_Char_State st = {0};
    st.time = 1;
    f32 trig_val = mugen_expr_eval(velset1->trigger_groups[0].conditions[0], &st);
    MEL_ASSERT_FLOAT_EQ(trig_val, 1.0f, 0.001f);

    Mugen_SC_Reg* reg = mugen_sc_get_reg(MUGEN_SC_VELSET);
    reg->exec(velset1, &st);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 6.0f, 0.001f);

    st.time = 0;
    trig_val = mugen_expr_eval(velset1->trigger_groups[0].conditions[0], &st);
    MEL_ASSERT_FLOAT_EQ(trig_val, 0.0f, 0.001f);

    free(data.data);
}

MEL_TEST(cns_tick_state421_slide, .tags = "cns")
{
    ensure_alloc();
    str8 data = load_file("demos/street-carlos/chars/poi-son/poi-son.cns");
    MEL_ASSERT(data.len > 0);

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, data, s_alloc);

    Mugen_Char_State st = {0};
    st.gravity = 0.5f;
    st.stand_friction = 0.85f;
    st.facing = 1.0f;
    st.alive = true;
    st.life = 1000; st.lifemax = 1000;

    mugen_cns_enter_state(&cns, &st, 421);
    MEL_ASSERT_EQ(st.stateno, 421);
    MEL_ASSERT_EQ(st.statetype, MUGEN_PHYSICS_C);
    MEL_ASSERT_EQ(st.movetype, MUGEN_MOVETYPE_A);
    MEL_ASSERT_EQ(st.physics, MUGEN_PHYSICS_N);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 0.0f, 0.001f);

    st.animtime = -10;

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.stateno, 421);
    MEL_ASSERT_EQ(st.time, 1);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 0.0f, 0.001f);

    mugen_cns_tick(&cns, &st);
    MEL_ASSERT_EQ(st.time, 2);
    MEL_ASSERT_FLOAT_EQ(st.vel_x, 6.0f, 0.001f);

    free(data.data);
}

static void load_both_cns(Mugen_Cns* char_cns, Mugen_Cns* common_cns, str8* char_data, str8* common_data)
{
    *char_data = load_file("demos/street-carlos/chars/poi-son/poi-son.cns");
    *common_data = load_file("demos/street-carlos/chars/poi-son/common-one.cns");
    mugen_cns_load(char_cns, *char_data, s_alloc);
    mugen_cns_load(common_cns, *common_data, s_alloc);
}

static void init_standing_state(Mugen_Char_State* st)
{
    memset(st, 0, sizeof(*st));
    st->gravity = 0.44f;
    st->stand_friction = 0.85f;
    st->facing = 1.0f;
    st->alive = true;
    st->ctrl = true;
    st->life = 1000; st->lifemax = 1000;
    st->powermax = 3000;
    st->statetype = MUGEN_PHYSICS_S;
    st->physics = MUGEN_PHYSICS_S;
    st->jump_y = 8.4f;
    st->jump_neu_x = 0.0f;
    st->jump_fwd_x = 2.0f;
    st->jump_back_x = -2.0f;
    st->walk_fwd_x = 2.0f;
    st->walk_back_x = -2.0f;
    st->roundstate = 2;
}

MEL_TEST(cns_attack_state_persists, .tags = "cns")
{
    ensure_alloc();
    Mugen_Cns char_cns = {0}, common_cns = {0};
    str8 cd, comd;
    load_both_cns(&char_cns, &common_cns, &cd, &comd);

    Mugen_Char_State st;
    init_standing_state(&st);

    mugen_cns_enter_state(&char_cns, &st, 200);
    MEL_ASSERT_EQ(st.stateno, 200);
    MEL_ASSERT_EQ(st.ctrl, false);

    st.animtime = -20;

    mugen_cns_tick(&char_cns, &st);
    MEL_ASSERT_EQ(st.stateno, 200);

    st.animtime = -15;
    mugen_cns_tick(&char_cns, &st);
    MEL_ASSERT_EQ(st.stateno, 200);

    st.animtime = -10;
    mugen_cns_tick(&char_cns, &st);
    MEL_ASSERT_EQ(st.stateno, 200);

    st.animtime = -5;
    mugen_cns_tick(&char_cns, &st);
    MEL_ASSERT_EQ(st.stateno, 200);

    free(cd.data);
    free(comd.data);
}

MEL_TEST(cns_attack_animtime0_exits, .tags = "cns")
{
    ensure_alloc();
    Mugen_Cns char_cns = {0}, common_cns = {0};
    str8 cd, comd;
    load_both_cns(&char_cns, &common_cns, &cd, &comd);

    Mugen_Char_State st;
    init_standing_state(&st);

    mugen_cns_enter_state(&char_cns, &st, 200);
    st.animtime = -20;

    for (i32 i = 0; i < 5; i++)
    {
        st.animtime = -20 + i * 5;
        mugen_cns_tick(&char_cns, &st);
    }
    MEL_ASSERT_EQ(st.stateno, 200);

    st.animtime = 0;
    mugen_cns_tick(&char_cns, &st);
    MEL_ASSERT(st.state_changed);
    MEL_ASSERT_EQ(st.pending_state, 0);

    free(cd.data);
    free(comd.data);
}

MEL_TEST(cns_attack_stale_animtime_should_not_exit, .tags = "cns")
{
    ensure_alloc();
    Mugen_Cns char_cns = {0}, common_cns = {0};
    str8 cd, comd;
    load_both_cns(&char_cns, &common_cns, &cd, &comd);

    Mugen_Char_State st;
    init_standing_state(&st);

    st.animtime = 0;

    mugen_cns_enter_state(&char_cns, &st, 200);

    mugen_cns_tick(&char_cns, &st);

    MEL_ASSERT_EQ(st.state_changed, false);

    free(cd.data);
    free(comd.data);
}

MEL_TEST(cns_jump_state40_to_50, .tags = "cns")
{
    ensure_alloc();
    Mugen_Cns char_cns = {0}, common_cns = {0};
    str8 cd, comd;
    load_both_cns(&char_cns, &common_cns, &cd, &comd);

    Mugen_Char_State st;
    init_standing_state(&st);

    mugen_cns_enter_state(&common_cns, &st, 40);
    MEL_ASSERT_EQ(st.stateno, 40);
    MEL_ASSERT_EQ(st.ctrl, false);

    st.animtime = 0;
    mugen_cns_tick(&common_cns, &st);

    MEL_ASSERT(st.state_changed);
    MEL_ASSERT_EQ(st.pending_state, 50);

    MEL_ASSERT(st.vel_y != 0.0f);

    free(cd.data);
    free(comd.data);
}

MEL_TEST(cns_jump_physics_a_goes_airborne, .tags = "cns")
{
    ensure_alloc();
    Mugen_Cns char_cns = {0}, common_cns = {0};
    str8 cd, comd;
    load_both_cns(&char_cns, &common_cns, &cd, &comd);

    Mugen_Char_State st;
    init_standing_state(&st);

    mugen_cns_enter_state(&common_cns, &st, 50);
    MEL_ASSERT_EQ(st.statetype, MUGEN_PHYSICS_A);
    MEL_ASSERT_EQ(st.physics, MUGEN_PHYSICS_A);

    st.vel_y = -8.4f;
    st.animtime = -60;

    mugen_cns_tick(&common_cns, &st);
    MEL_ASSERT(st.pos_y < 0.0f);

    for (i32 i = 0; i < 5; i++)
    {
        st.animtime = -60;
        mugen_cns_tick(&common_cns, &st);
    }
    MEL_ASSERT(st.pos_y < 0.0f);


    free(cd.data);
    free(comd.data);
}

MEL_TEST(cns_jump_physics_a_lands, .tags = "cns")
{
    ensure_alloc();
    Mugen_Cns char_cns = {0}, common_cns = {0};
    str8 cd, comd;
    load_both_cns(&char_cns, &common_cns, &cd, &comd);

    Mugen_Char_State st;
    init_standing_state(&st);

    mugen_cns_enter_state(&common_cns, &st, 50);
    st.vel_y = -8.4f;
    st.animtime = -999;

    bool went_airborne = false;
    bool landed = false;
    for (i32 i = 0; i < 100; i++)
    {
        st.animtime = -999;
        mugen_cns_tick(&common_cns, &st);
        if (st.pos_y < -1.0f) went_airborne = true;
        if (went_airborne && st.state_changed && st.pending_state == 52)
        {
            landed = true;
            break;
        }
    }

    MEL_ASSERT(went_airborne);
    MEL_ASSERT(landed);
    MEL_ASSERT_FLOAT_EQ(st.pos_y, 0.0f, 0.001f);

    free(cd.data);
    free(comd.data);
}

MEL_TEST(cns_parse_constants_kfm, .tags = "cns")
{
    ensure_alloc();
    str8 data = load_file("demos/street-carlos/chars/kfm/kfm.cns");
    MEL_ASSERT(data.len > 0);

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, data, s_alloc);

    MEL_ASSERT(cns.has_constants);
    Mugen_Char_Constants* c = &cns.constants;
    MEL_ASSERT_EQ(c->life, 1000);
    MEL_ASSERT_EQ(c->attack, 100);
    MEL_ASSERT_EQ(c->defence, 100);
    MEL_ASSERT_EQ(c->sparkno, 2);
    MEL_ASSERT_FLOAT_EQ(c->ground_back, 15.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->ground_front, 16.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->height, 60.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->walk_fwd_x, 2.4f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->walk_back_x, -2.2f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->run_fwd_x, 4.6f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->run_back_x, -4.5f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->run_back_y, 3.8f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->jump_y, 8.4f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->jump_back_x, -2.55f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->jump_fwd_x, 2.5f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->yaccel, 0.44f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->stand_friction, 0.85f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->crouch_friction, 0.82f, 0.01f);

    free(data.data);
}

MEL_TEST(cns_parse_constants_poison, .tags = "cns")
{
    ensure_alloc();
    str8 data = load_file("demos/street-carlos/chars/poi-son/poi-son.cns");
    MEL_ASSERT(data.len > 0);

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, data, s_alloc);

    MEL_ASSERT(cns.has_constants);
    Mugen_Char_Constants* c = &cns.constants;
    MEL_ASSERT_EQ(c->life, 1000);
    MEL_ASSERT_FLOAT_EQ(c->ground_back, 18.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->ground_front, 19.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->walk_fwd_x, 2.4f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->walk_back_x, -2.1f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(c->jump_y, 9.6f, 0.01f);

    free(data.data);
}

MEL_TEST(cns_helper_controller_parsed, .tags = "cns")
{
    ensure_alloc();
    str8 data = load_file("demos/street-carlos/chars/poi-son/poi-son.cns");
    MEL_ASSERT(data.len > 0);

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, data, s_alloc);

    bool found_helper = false;
    bool found_destroyself = false;
    for (u32 s = 0; s < cns.statedef_count; s++)
    {
        Mugen_Statedef* def = &cns.statedefs[s];
        for (u32 c = 0; c < def->controller_count; c++)
        {
            if (def->controllers[c].type == MUGEN_SC_HELPER)
                found_helper = true;
            if (def->controllers[c].type == MUGEN_SC_DESTROYSELF)
                found_destroyself = true;
        }
    }
    MEL_ASSERT(found_helper);
    MEL_ASSERT(found_destroyself);

    free(data.data);
}

static i32 test_query_num_helper_zero(void* ctx, i32 id)
{
    (void)ctx; (void)id;
    return 0;
}

MEL_TEST(cns_helper_spawn_request, .tags = "cns")
{
    ensure_alloc();
    str8 data = load_file("demos/street-carlos/chars/poi-son/poi-son.cns");
    MEL_ASSERT(data.len > 0);

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, data, s_alloc);

    bool found_helper_sc = false;
    Mugen_Statedef* helper_def = NULL;
    for (u32 s = 0; s < cns.statedef_count && !found_helper_sc; s++)
    {
        Mugen_Statedef* def = &cns.statedefs[s];
        for (u32 c = 0; c < def->controller_count; c++)
        {
            if (def->controllers[c].type == MUGEN_SC_HELPER)
            {
                found_helper_sc = true;
                helper_def = def;
                break;
            }
        }
    }
    MEL_ASSERT(found_helper_sc);
    MEL_ASSERT_NOT_NULL(helper_def);

    Mugen_Char_State st;
    init_standing_state(&st);
    st.statetype = MUGEN_PHYSICS_S;
    st.animtime = -999;
    st.rng_state = 12345;
    st.fvar[11] = 2.0f;
    st.query_num_helper = test_query_num_helper_zero;
    st.helper_ctx = NULL;

    mugen_cns_enter_state(&cns, &st, helper_def->stateno);

    for (i32 i = 0; i < 20; i++)
    {
        st.animtime = -999;
        mugen_cns_tick(&cns, &st);
        if (st.helper_spawn_pending) break;
        if (st.state_changed)
        {
            mugen_cns_enter_state(&cns, &st, st.pending_state);
            st.state_changed = false;
        }
    }

    MEL_ASSERT(st.helper_spawn_pending);
    MEL_ASSERT(st.helper_spawn_id != 0);

    free(data.data);
}

static i32 test_query_num_helper(void* ctx, i32 id)
{
    (void)ctx;
    return (id == 42) ? 2 : 0;
}

static Mugen_Char_State s_test_helper_state;
static Mugen_Char_State* test_query_helper_state(void* ctx, i32 id)
{
    (void)ctx;
    if (id == 42) return &s_test_helper_state;
    return NULL;
}

static Mugen_Char_State s_test_root_state;
static Mugen_Char_State* test_query_root_state(void* ctx)
{
    (void)ctx;
    return &s_test_root_state;
}

MEL_TEST(cns_numhelper_query, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.query_num_helper = test_query_num_helper;
    st.helper_ctx = NULL;

    Mugen_Expr* e = mugen_expr_parse(S8("NumHelper(42)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 2.0f, 0.001f);

    e = mugen_expr_parse(S8("NumHelper(99)"), s_alloc);
    val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 0.0f, 0.001f);
}

MEL_TEST(cns_helper_redirect_var, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.query_helper_state = test_query_helper_state;
    st.helper_ctx = NULL;

    memset(&s_test_helper_state, 0, sizeof(s_test_helper_state));
    s_test_helper_state.var[4] = 77;

    Mugen_Expr* e = mugen_expr_parse(S8("helper(42), var(4)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_EQ(e->type, MUGEN_EXPR_REDIRECT);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 77.0f, 0.001f);
}

MEL_TEST(cns_root_redirect_stateno, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.is_helper = true;
    st.query_root_state = test_query_root_state;
    st.helper_ctx = NULL;

    memset(&s_test_root_state, 0, sizeof(s_test_root_state));
    s_test_root_state.stateno = 200;

    Mugen_Expr* e = mugen_expr_parse(S8("root, stateno"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_EQ(e->type, MUGEN_EXPR_REDIRECT);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 200.0f, 0.001f);
}

MEL_TEST(cns_helper_no_redirect_is_numhelper, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.query_num_helper = test_query_num_helper;
    st.helper_ctx = NULL;

    Mugen_Expr* e = mugen_expr_parse(S8("helper(42)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_EQ(e->type, MUGEN_EXPR_QUERY);
    MEL_ASSERT_EQ(e->query.id, MUGEN_QUERY_NUMHELPER);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 2.0f, 0.001f);
}

MEL_TEST(cns_range_inclusive, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.stateno = 210;

    Mugen_Expr* e = mugen_expr_parse(S8("stateno = [200, 299]"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.stateno = 200;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.stateno = 299;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.stateno = 199;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    st.stateno = 300;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_range_exclusive, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};

    Mugen_Expr* e = mugen_expr_parse(S8("stateno = (10, 20)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    st.stateno = 15;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.stateno = 10;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    st.stateno = 20;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_range_not_in, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};

    Mugen_Expr* e = mugen_expr_parse(S8("stateno != [200, 299]"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    st.stateno = 150;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.stateno = 210;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_range_mixed_bounds, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};

    Mugen_Expr* e = mugen_expr_parse(S8("stateno = [10, 20)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    st.stateno = 10;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.stateno = 19;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.stateno = 20;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_range_real_pattern, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.anim = 5055;

    Mugen_Expr* e = mugen_expr_parse(S8("anim = [5051,5059]"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.anim = 5060;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_animelem_first_tick, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    i32 ticks[] = {0, 5, 10};
    st.anim_elem_start_ticks = ticks;
    st.anim_elem_count = 3;
    st.time = 5;

    Mugen_Expr* e = mugen_expr_parse(S8("AnimElem = 2"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.time = 6;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    st.time = 4;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_animelem_compound_lt, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    i32 ticks[] = {0, 5, 10, 15};
    st.anim_elem_start_ticks = ticks;
    st.anim_elem_count = 4;

    Mugen_Expr* e = mugen_expr_parse(S8("AnimElem = 4, < 0"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    st.time = 14;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.time = 15;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    st.time = 16;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_animelem_compound_ge, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    i32 ticks[] = {0, 5, 10};
    st.anim_elem_start_ticks = ticks;
    st.anim_elem_count = 3;

    Mugen_Expr* e = mugen_expr_parse(S8("AnimElem = 2, >= 0"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    st.time = 4;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    st.time = 5;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.time = 8;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);
}

MEL_TEST(cns_animelem_compound_eq, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    i32 ticks[] = {0, 5, 10};
    st.anim_elem_start_ticks = ticks;
    st.anim_elem_count = 3;

    Mugen_Expr* e = mugen_expr_parse(S8("AnimElem = 3, = 1"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);

    st.time = 10;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    st.time = 11;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    st.time = 12;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_destroyself_sets_flag, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.is_helper = true;
    st.destroy_self_pending = false;

    str8 cns_text = S8(
        "[Statedef 9999]\n"
        "type = S\n"
        "\n"
        "[State 9999, die]\n"
        "type = DestroySelf\n"
        "trigger1 = 1\n"
    );

    Mugen_Cns cns = {0};
    mugen_cns_load(&cns, cns_text, s_alloc);

    mugen_cns_enter_state(&cns, &st, 9999);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT(st.destroy_self_pending);
}

MEL_TEST(cns_hitdefattr_parse_bitmask, .tags = "cns")
{
    ensure_alloc();

    Mugen_Expr* e = mugen_expr_parse(S8("hitdefattr = SC, NA, SA, HA"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_EQ(e->type, MUGEN_EXPR_BINARY);
    MEL_ASSERT_EQ(e->binary.op, MUGEN_OP_EQ);
    MEL_ASSERT_EQ(e->binary.lhs->type, MUGEN_EXPR_QUERY);
    MEL_ASSERT_EQ(e->binary.lhs->query.id, MUGEN_QUERY_HITDEFATTR);
    MEL_ASSERT_EQ(e->binary.rhs->type, MUGEN_EXPR_LIT_INT);

    u32 expected = MUGEN_ATTR_S | MUGEN_ATTR_C | MUGEN_ATTR_NA | MUGEN_ATTR_SA | MUGEN_ATTR_HA;
    MEL_ASSERT_EQ((u32)e->binary.rhs->lit_int, expected);
}

MEL_TEST(cns_hitdefattr_match_active, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.movetype = MUGEN_MOVETYPE_A;
    st.hitdef_active = true;
    st.hitdef.attr = MUGEN_ATTR_S | MUGEN_ATTR_NA;

    Mugen_Expr* e = mugen_expr_parse(S8("hitdefattr = SC, NA, SA, HA"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);
}

MEL_TEST(cns_hitdefattr_no_match_wrong_attack, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.movetype = MUGEN_MOVETYPE_A;
    st.hitdef_active = true;
    st.hitdef.attr = MUGEN_ATTR_S | MUGEN_ATTR_NP;

    Mugen_Expr* e = mugen_expr_parse(S8("hitdefattr = SC, NA, SA, HA"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_hitdefattr_no_match_wrong_state, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.movetype = MUGEN_MOVETYPE_A;
    st.hitdef_active = true;
    st.hitdef.attr = MUGEN_ATTR_A | MUGEN_ATTR_NA;

    Mugen_Expr* e = mugen_expr_parse(S8("hitdefattr = SC, NA, SA, HA"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_hitdefattr_inactive_hitdef, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.movetype = MUGEN_MOVETYPE_I;
    st.hitdef_active = false;
    st.hitdef.attr = MUGEN_ATTR_S | MUGEN_ATTR_NA;

    Mugen_Expr* e = mugen_expr_parse(S8("hitdefattr = SC, NA, SA, HA"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_hitdefattr_neq, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.movetype = MUGEN_MOVETYPE_A;
    st.hitdef_active = true;
    st.hitdef.attr = MUGEN_ATTR_S | MUGEN_ATTR_NA;

    Mugen_Expr* e = mugen_expr_parse(S8("hitdefattr != SC, NA, SA"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    st.hitdef.attr = MUGEN_ATTR_S | MUGEN_ATTR_NP;
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);
}

MEL_TEST(cns_expr_bitwise_not, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("~0"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, (f32)(~0), 0.001f);

    e = mugen_expr_parse(S8("~255"), s_alloc);
    val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, (f32)(~255), 0.001f);
}

MEL_TEST(cns_expr_bitwise_xor, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("5 ^ 3"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, (f32)(5 ^ 3), 0.001f);

    e = mugen_expr_parse(S8("255 ^ 128"), s_alloc);
    val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, (f32)(255 ^ 128), 0.001f);
}

MEL_TEST(cns_expr_cond_short_circuit, .tags = "cns")
{
    ensure_alloc();
    Mugen_Expr* e = mugen_expr_parse(S8("cond(1, 10, 20)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 10.0f, 0.001f);

    e = mugen_expr_parse(S8("cond(0, 10, 20)"), s_alloc);
    val = mugen_expr_eval(e, NULL);
    MEL_ASSERT_FLOAT_EQ(val, 20.0f, 0.001f);
}

MEL_TEST(cns_expr_assign_var, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};

    Mugen_Expr* e = mugen_expr_parse(S8("var(0) := 42"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 42.0f, 0.001f);
    MEL_ASSERT_EQ(st.var[0], 42);

    e = mugen_expr_parse(S8("fvar(3) := 1.5"), s_alloc);
    val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 1.5f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(st.fvar[3], 1.5f, 0.001f);
}

MEL_TEST(cns_expr_assign_returns_value, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};

    Mugen_Expr* e = mugen_expr_parse(S8("(var(0) := 5) + 10"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    f32 val = mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(val, 15.0f, 0.001f);
    MEL_ASSERT_EQ(st.var[0], 5);
}

MEL_TEST(cns_expr_assign_sysvar, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};

    Mugen_Expr* e = mugen_expr_parse(S8("sysvar(0) := 99"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    mugen_expr_eval(e, &st);
    MEL_ASSERT_EQ(st.sysvar[0], 99);

    e = mugen_expr_parse(S8("sysfvar(2) := 7.5"), s_alloc);
    mugen_expr_eval(e, &st);
    MEL_ASSERT_FLOAT_EQ(st.sysfvar[2], 7.5f, 0.001f);
}

MEL_TEST(cns_query_physics, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.physics = MUGEN_PHYSICS_A;

    Mugen_Expr* e = mugen_expr_parse(S8("physics = A"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    e = mugen_expr_parse(S8("physics = S"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);

    st.physics = MUGEN_PHYSICS_N;
    e = mugen_expr_parse(S8("physics = N"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);
}

MEL_TEST(cns_sc_lifeadd, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.life = 500;
    st.lifemax = 1000;

    str8 cns_data = S8(
        "[Statedef 9000]\n"
        "type = S\n"
        "[State 9000, LifeAdd]\n"
        "type = LifeAdd\n"
        "trigger1 = 1\n"
        "value = -100\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));
    Mugen_Statedef* def = mugen_cns_get(&cns, 9000);
    MEL_ASSERT_NOT_NULL(def);
    mugen_cns_tick_statedef(def, &st);
    MEL_ASSERT_FLOAT_EQ(st.life, 400.0f, 0.001f);
}

MEL_TEST(cns_sc_powerset, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.power = 500;
    st.powermax = 3000;

    str8 cns_data = S8(
        "[Statedef 9001]\n"
        "type = S\n"
        "[State 9001, PowerSet]\n"
        "type = PowerSet\n"
        "trigger1 = 1\n"
        "value = 1000\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));
    Mugen_Statedef* def = mugen_cns_get(&cns, 9001);
    MEL_ASSERT_NOT_NULL(def);
    mugen_cns_tick_statedef(def, &st);
    MEL_ASSERT_FLOAT_EQ(st.power, 1000.0f, 0.001f);
}

MEL_TEST(cns_sc_movehitreset, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.mctime = 5;
    st.movehit = 3;
    st.moveguarded = 2;

    str8 cns_data = S8(
        "[Statedef 9002]\n"
        "type = S\n"
        "[State 9002, MoveHitReset]\n"
        "type = MoveHitReset\n"
        "trigger1 = 1\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));
    Mugen_Statedef* def = mugen_cns_get(&cns, 9002);
    MEL_ASSERT_NOT_NULL(def);
    mugen_cns_tick_statedef(def, &st);
    MEL_ASSERT_EQ(st.mctime, 0);
    MEL_ASSERT_EQ(st.movehit, 0);
    MEL_ASSERT_EQ(st.moveguarded, 0);
}

MEL_TEST(cns_sc_attackmulset, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.attack_mul = 1.0f;

    str8 cns_data = S8(
        "[Statedef 9003]\n"
        "type = S\n"
        "[State 9003, AttackMulSet]\n"
        "type = AttackMulSet\n"
        "trigger1 = 1\n"
        "value = 1.5\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));
    Mugen_Statedef* def = mugen_cns_get(&cns, 9003);
    MEL_ASSERT_NOT_NULL(def);
    mugen_cns_tick_statedef(def, &st);
    MEL_ASSERT_FLOAT_EQ(st.attack_mul, 1.5f, 0.001f);
}

MEL_TEST(cns_sc_hitadd, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.hitcount = 3;

    str8 cns_data = S8(
        "[Statedef 9004]\n"
        "type = S\n"
        "[State 9004, HitAdd]\n"
        "type = HitAdd\n"
        "trigger1 = 1\n"
        "value = 2\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));
    Mugen_Statedef* def = mugen_cns_get(&cns, 9004);
    MEL_ASSERT_NOT_NULL(def);
    mugen_cns_tick_statedef(def, &st);
    MEL_ASSERT_EQ(st.hitcount, 5);
}

MEL_TEST(cns_query_prevstatetype, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.prev_statetype = MUGEN_PHYSICS_S;
    st.statetype = MUGEN_PHYSICS_A;

    Mugen_Expr* e = mugen_expr_parse(S8("prevstatetype = S"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    e = mugen_expr_parse(S8("prevstatetype = A"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_query_prevmovetype, .tags = "cns")
{
    ensure_alloc();
    Mugen_Char_State st = {0};
    st.prev_movetype = MUGEN_MOVETYPE_A;
    st.movetype = MUGEN_MOVETYPE_I;

    Mugen_Expr* e = mugen_expr_parse(S8("prevmovetype = A"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    e = mugen_expr_parse(S8("prevmovetype = I"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_query_animlength, .tags = "cns")
{
    ensure_alloc();
    Mugen_Air_Frame frames[] = {
        { .time = 5 },
        { .time = 10 },
        { .time = 3 },
    };
    Mugen_Air_Action action = {
        .frames = frames,
        .frame_count = 3,
    };
    Mugen_Char_State st = {0};
    st.anim_action = &action;

    Mugen_Expr* e = mugen_expr_parse(S8("animlength"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 18.0f, 0.001f);
}

MEL_TEST(cns_query_selfstatenoexist, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.self_cns = &cns;

    Mugen_Expr* e = mugen_expr_parse(S8("selfstatenoexist(200)"), s_alloc);
    MEL_ASSERT_NOT_NULL(e);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 1.0f, 0.001f);

    e = mugen_expr_parse(S8("selfstatenoexist(999)"), s_alloc);
    MEL_ASSERT_FLOAT_EQ(mugen_expr_eval(e, &st), 0.0f, 0.001f);
}

MEL_TEST(cns_sc_envshake, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = EnvShake\n"
        "trigger1 = Time = 0\n"
        "time = 8\n"
        "ampl = 3\n"
        "freq = 170\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ(st.envshake_time, 7);
    MEL_ASSERT_FLOAT_EQ(st.envshake_ampl, 3.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(st.envshake_freq, 170.0f, 0.001f);
}

MEL_TEST(cns_sc_palfx, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = PalFX\n"
        "trigger1 = Time = 0\n"
        "time = 3\n"
        "add = 128,128,128\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ(st.palfx_time, 2);
    MEL_ASSERT_EQ(st.palfx_add[0], 128);
    MEL_ASSERT_EQ(st.palfx_add[1], 128);
    MEL_ASSERT_EQ(st.palfx_add[2], 128);
    MEL_ASSERT_EQ(st.palfx_mul[0], 256);
    MEL_ASSERT_EQ(st.palfx_mul[1], 256);
    MEL_ASSERT_EQ(st.palfx_mul[2], 256);
}

MEL_TEST(cns_sc_palfx_sinadd, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = PalFX\n"
        "trigger1 = Time = 0\n"
        "time = 20\n"
        "add = 32,16,0\n"
        "sinadd = 64,32,5,3\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ(st.palfx_time, 19);
    MEL_ASSERT_EQ(st.palfx_add[0], 32);
    MEL_ASSERT_EQ(st.palfx_add[1], 16);
    MEL_ASSERT_EQ(st.palfx_add[2], 0);
    MEL_ASSERT_EQ(st.palfx_sinadd[0], 64);
    MEL_ASSERT_EQ(st.palfx_sinadd[1], 32);
    MEL_ASSERT_EQ(st.palfx_sinadd[2], 5);
    MEL_ASSERT_EQ(st.palfx_sinadd_period, 3);
}

MEL_TEST(cns_sc_screenbound, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = ScreenBound\n"
        "trigger1 = 1\n"
        "value = 1\n"
        "movecamera = 0,1\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT(st.screenbound_value);
    MEL_ASSERT(!st.screenbound_movecamera_x);
    MEL_ASSERT(st.screenbound_movecamera_y);
}

MEL_TEST(cns_sc_pause, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = Pause\n"
        "trigger1 = Time = 0\n"
        "time = 20\n"
        "endcmdbuftime = 20\n"
        "pausebg = 0\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ(st.pause_time, 19);
    MEL_ASSERT_EQ(st.pause_endcmdbuftime, 20);
    MEL_ASSERT(!st.pause_bg);
}

MEL_TEST(cns_sc_playerpush, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = PlayerPush\n"
        "trigger1 = 1\n"
        "value = 0\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    st.playerpush = true;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT(!st.playerpush);
}

MEL_TEST(cns_sc_attackdist, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = AttackDist\n"
        "trigger1 = 1\n"
        "value = 200\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_FLOAT_EQ(st.attack_dist_override, 200.0f, 0.01f);
}

MEL_TEST(cns_sc_angle, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, set]\n"
        "type = AngleSet\n"
        "trigger1 = 1\n"
        "value = 45\n"
        "\n"
        "[State 200, add]\n"
        "type = AngleAdd\n"
        "trigger1 = 1\n"
        "value = 10\n"
        "\n"
        "[State 200, mul]\n"
        "type = AngleMul\n"
        "trigger1 = 1\n"
        "value = 2\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_FLOAT_EQ(st.angle, 110.0f, 0.01f);
}

MEL_TEST(cns_sc_angledraw, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = AngleDraw\n"
        "trigger1 = 1\n"
        "scale = 2.0, 0.5\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT(st.angle_draw);
    MEL_ASSERT_FLOAT_EQ(st.angle_draw_xscale, 2.0f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(st.angle_draw_yscale, 0.5f, 0.01f);
}

MEL_TEST(cns_sc_trans_addalpha, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = Trans\n"
        "trigger1 = 1\n"
        "trans = addalpha\n"
        "alpha = 128, 64\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ(st.trans_type, MUGEN_TRANS_ADDALPHA);
    MEL_ASSERT_EQ(st.trans_alpha_src, 128);
    MEL_ASSERT_EQ(st.trans_alpha_dst, 64);
}

MEL_TEST(cns_sc_trans_add, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = Trans\n"
        "trigger1 = 1\n"
        "trans = add\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ(st.trans_type, MUGEN_TRANS_ADD);
    MEL_ASSERT_EQ(st.trans_alpha_src, 256);
    MEL_ASSERT_EQ(st.trans_alpha_dst, 256);
}

MEL_TEST(cns_sc_gethitvarset, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = GetHitVarSet\n"
        "trigger1 = 1\n"
        "xvel = 5.5\n"
        "yvel = -3.0\n"
        "fall.damage = 20\n"
        "fall.recover = 0\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    st.ghv.fall_recover = true;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_FLOAT_EQ(st.ghv.xvel, 5.5f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(st.ghv.yvel, -3.0f, 0.01f);
    MEL_ASSERT_EQ(st.ghv.fall_damage, 20);
    MEL_ASSERT(!st.ghv.fall_recover);
}

MEL_TEST(cns_sc_afterimage, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = AfterImage\n"
        "trigger1 = Time = 0\n"
        "time = 10\n"
        "length = 13\n"
        "PalBright = 30,30,0\n"
        "PalContrast = 70,70,20\n"
        "PalAdd = -10,-10,-10\n"
        "PalMul = .85,.85,.50\n"
        "TimeGap = 1\n"
        "FrameGap = 2\n"
        "Trans = Add\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ(st.afterimage.time, 9);
    MEL_ASSERT_EQ(st.afterimage.length, 13);
    MEL_ASSERT_EQ(st.afterimage.timegap, 1);
    MEL_ASSERT_EQ(st.afterimage.framegap, 2);
    MEL_ASSERT_EQ(st.afterimage.trans, MUGEN_TRANS_ADD);
    MEL_ASSERT_EQ(st.afterimage.palbright[0], 30);
    MEL_ASSERT_EQ(st.afterimage.palbright[2], 0);
    MEL_ASSERT_EQ(st.afterimage.palcontrast[0], 70);
    MEL_ASSERT_EQ(st.afterimage.palcontrast[2], 20);
    MEL_ASSERT_EQ(st.afterimage.paladd[0], -10);
    MEL_ASSERT_FLOAT_EQ(st.afterimage.palmul[0], 0.85f, 0.01f);
    MEL_ASSERT_FLOAT_EQ(st.afterimage.palmul[2], 0.50f, 0.01f);
}

MEL_TEST(cns_afterimage_record, .tags = "cns")
{
    Mugen_Char_State st = {0};
    st.afterimage.time = 5;
    st.afterimage.length = 4;
    st.afterimage.timegap = 1;
    st.afterimage.framegap = 1;
    st.pos_x = 10.0f;
    st.pos_y = 0.0f;
    st.facing = 1.0f;
    st.anim = 100;

    mugen_afterimage_record(&st);
    MEL_ASSERT_EQ(st.afterimage.frame_count, 1);
    MEL_ASSERT_EQ(st.afterimage.time, 4);

    st.pos_x = 20.0f;
    mugen_afterimage_record(&st);
    MEL_ASSERT_EQ(st.afterimage.frame_count, 2);

    st.pos_x = 30.0f;
    mugen_afterimage_record(&st);
    st.pos_x = 40.0f;
    mugen_afterimage_record(&st);
    MEL_ASSERT_EQ(st.afterimage.frame_count, 4);

    st.pos_x = 50.0f;
    mugen_afterimage_record(&st);
    MEL_ASSERT_EQ(st.afterimage.frame_count, 4);

    Mugen_AfterImage_Snap* snap = mugen_afterimage_get(&st, 0);
    MEL_ASSERT_NOT_NULL(snap);
    MEL_ASSERT_FLOAT_EQ(snap->pos_x, 50.0f, 0.01f);

    snap = mugen_afterimage_get(&st, 1);
    MEL_ASSERT_NOT_NULL(snap);
    MEL_ASSERT_FLOAT_EQ(snap->pos_x, 40.0f, 0.01f);

    mugen_afterimage_free(&st);
    MEL_ASSERT_EQ(st.afterimage.frame_count, 0);
    MEL_ASSERT_EQ(st.afterimage.time, 0);
}

MEL_TEST(cns_afterimage_timegap, .tags = "cns")
{
    Mugen_Char_State st = {0};
    st.afterimage.time = 10;
    st.afterimage.length = 10;
    st.afterimage.timegap = 3;
    st.afterimage.framegap = 1;
    st.pos_x = 0.0f;
    st.facing = 1.0f;

    for (i32 i = 0; i < 6; i++)
    {
        st.pos_x = (f32)i;
        mugen_afterimage_record(&st);
    }
    MEL_ASSERT_EQ(st.afterimage.frame_count, 2);

    Mugen_AfterImage_Snap* snap = mugen_afterimage_get(&st, 0);
    MEL_ASSERT_NOT_NULL(snap);
    MEL_ASSERT_FLOAT_EQ(snap->pos_x, 3.0f, 0.01f);
}

MEL_TEST(cns_sc_afterimagetime, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = AfterImageTime\n"
        "trigger1 = 1\n"
        "time = 5\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    st.afterimage.time = 0;
    mugen_cns_enter_state(&cns, &st, 200);
    mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ(st.afterimage.time, 4);
}

MEL_TEST(cns_afterimage_integrated, .tags = "cns")
{
    ensure_alloc();
    str8 cns_data = S8(
        "[Statedef 200]\n"
        "type = S\n"
        "\n"
        "[State 200, 1]\n"
        "type = AfterImage\n"
        "trigger1 = Time = 0\n"
        "time = 8\n"
        "length = 5\n"
        "TimeGap = 1\n"
        "FrameGap = 1\n"
        "Trans = Add\n"
    );
    Mugen_Cns cns = {0};
    MEL_ASSERT(mugen_cns_load(&cns, cns_data, s_alloc));

    Mugen_Char_State st = {0};
    st.stand_friction = 0.85f;
    st.vel_x = 5.0f;
    st.facing = 1.0f;
    mugen_cns_enter_state(&cns, &st, 200);

    for (i32 i = 0; i < 6; i++)
        mugen_cns_tick(&cns, &st);

    MEL_ASSERT_EQ(st.afterimage.frame_count, 5);
    MEL_ASSERT_EQ(st.afterimage.time, 2);

    Mugen_AfterImage_Snap* newest = mugen_afterimage_get(&st, 0);
    MEL_ASSERT_NOT_NULL(newest);
    MEL_ASSERT_GT(newest->pos_x, 0.0f);

    Mugen_AfterImage_Snap* older = mugen_afterimage_get(&st, 1);
    MEL_ASSERT_NOT_NULL(older);
    MEL_ASSERT_LT(older->pos_x, newest->pos_x);

    mugen_afterimage_free(&st);
}


#endif
