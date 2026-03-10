#include "mugen_cns_parse.h"

typedef struct {
    u32 attr;
    u32 hitflag;
    u32 guardflag;
    u8 ground_type;
    u8 air_type;
    u8 animtype;
    u8 air_animtype;
    u8 fall_animtype;
    Mugen_Expr* damage_hit;
    Mugen_Expr* damage_guard;
    Mugen_Expr* pausetime_p1;
    Mugen_Expr* pausetime_p2;
    Mugen_Expr* guard_pausetime_p1;
    Mugen_Expr* guard_pausetime_p2;
    Mugen_Expr* sparkno;
    Mugen_Expr* guard_sparkno;
    Mugen_Expr* spark_x;
    Mugen_Expr* spark_y;
    Mugen_Expr* hitsound_group;
    Mugen_Expr* hitsound_index;
    Mugen_Expr* guardsound_group;
    Mugen_Expr* guardsound_index;
    Mugen_Expr* ground_slidetime;
    Mugen_Expr* ground_hittime;
    Mugen_Expr* ground_vel_x;
    Mugen_Expr* ground_vel_y;
    Mugen_Expr* guard_slidetime;
    Mugen_Expr* guard_hittime;
    Mugen_Expr* guard_ctrltime;
    Mugen_Expr* guard_velocity;
    Mugen_Expr* air_vel_x;
    Mugen_Expr* air_vel_y;
    Mugen_Expr* airguard_vel_x;
    Mugen_Expr* airguard_vel_y;
    Mugen_Expr* air_hittime;
    Mugen_Expr* air_fall;
    Mugen_Expr* fall;
    Mugen_Expr* fall_recover;
    Mugen_Expr* fall_recovertime;
    Mugen_Expr* fall_damage;
    Mugen_Expr* fall_vel_x;
    Mugen_Expr* fall_vel_y;
    Mugen_Expr* priority;
    Mugen_Expr* getpower_hit;
    Mugen_Expr* getpower_guard;
    Mugen_Expr* givepower_hit;
    Mugen_Expr* givepower_guard;
    Mugen_Expr* p1stateno;
    Mugen_Expr* p2stateno;
    Mugen_Expr* p2getp1state;
    Mugen_Expr* numhits;
    Mugen_Expr* hitonce;
    Mugen_Expr* forcestand;
    Mugen_Expr* ground_cornerpush;
    Mugen_Expr* air_cornerpush;
    Mugen_Expr* guard_cornerpush;
    Mugen_Expr* yaccel;
    Mugen_Expr* p1facing;
    Mugen_Expr* p2facing;
} HitDef_Params;

typedef struct {
    Mugen_Expr* x;
    Mugen_Expr* y;
} HitVelSet_Params;

typedef struct {
    Mugen_Expr* fall_xvel;
    Mugen_Expr* fall_yvel;
    Mugen_Expr* fall_recover;
    Mugen_Expr* fall_recovertime;
    Mugen_Expr* fall_damage;
    Mugen_Expr* fall_envshake_time;
    i8 xvel_set;
    i8 yvel_set;
} HitFallSet_Params;

static void hitdef_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(HitDef_Params));
    HitDef_Params* p = sc->params;

    if (str8_ieq_cstr(key, "attr")) p->attr = mcns_parse_attr(val);
    else if (str8_ieq_cstr(key, "hitflag")) p->hitflag = mcns_parse_hitflag(val);
    else if (str8_ieq_cstr(key, "guardflag")) p->guardflag = mcns_parse_hitflag(val);
    else if (str8_ieq_cstr(key, "animtype")) p->animtype = mcns_parse_animtype(val);
    else if (str8_ieq_cstr(key, "air.animtype")) p->air_animtype = mcns_parse_animtype(val);
    else if (str8_ieq_cstr(key, "fall.animtype")) p->fall_animtype = mcns_parse_animtype(val);
    else if (str8_ieq_cstr(key, "ground.type")) p->ground_type = mcns_parse_groundtype(val);
    else if (str8_ieq_cstr(key, "air.type")) p->air_type = mcns_parse_groundtype(val);
    else if (str8_ieq_cstr(key, "damage"))
    {
        str8 rest;
        str8 hit = mcns_split_comma_first(val, &rest);
        p->damage_hit = mugen_expr_parse(hit, alloc);
        if (rest.len > 0) p->damage_guard = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "pausetime"))
    {
        str8 rest;
        str8 p1 = mcns_split_comma_first(val, &rest);
        p->pausetime_p1 = mugen_expr_parse(p1, alloc);
        if (rest.len > 0) p->pausetime_p2 = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "guard.pausetime"))
    {
        str8 rest;
        str8 gp1 = mcns_split_comma_first(val, &rest);
        p->guard_pausetime_p1 = mugen_expr_parse(gp1, alloc);
        if (rest.len > 0) p->guard_pausetime_p2 = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "sparkno")) p->sparkno = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "guard.sparkno")) p->guard_sparkno = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "sparkxy"))
    {
        str8 rest;
        str8 sx = mcns_split_comma_first(val, &rest);
        p->spark_x = mugen_expr_parse(sx, alloc);
        if (rest.len > 0) p->spark_y = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "hitsound"))
    {
        str8 rest;
        str8 grp = mcns_split_comma_first(val, &rest);
        p->hitsound_group = mugen_expr_parse(grp, alloc);
        if (rest.len > 0) p->hitsound_index = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "guardsound"))
    {
        str8 rest;
        str8 grp = mcns_split_comma_first(val, &rest);
        p->guardsound_group = mugen_expr_parse(grp, alloc);
        if (rest.len > 0) p->guardsound_index = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "ground.slidetime")) p->ground_slidetime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "ground.hittime")) p->ground_hittime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "ground.velocity"))
    {
        str8 rest;
        str8 vx = mcns_split_comma_first(val, &rest);
        p->ground_vel_x = mugen_expr_parse(vx, alloc);
        if (rest.len > 0) p->ground_vel_y = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "guard.slidetime")) p->guard_slidetime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "guard.hittime")) p->guard_hittime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "guard.ctrltime")) p->guard_ctrltime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "guard.velocity")) p->guard_velocity = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "air.velocity"))
    {
        str8 rest;
        str8 vx = mcns_split_comma_first(val, &rest);
        p->air_vel_x = mugen_expr_parse(vx, alloc);
        if (rest.len > 0) p->air_vel_y = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "airguard.velocity"))
    {
        str8 rest;
        str8 vx = mcns_split_comma_first(val, &rest);
        p->airguard_vel_x = mugen_expr_parse(vx, alloc);
        if (rest.len > 0) p->airguard_vel_y = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "air.hittime")) p->air_hittime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "air.fall")) p->air_fall = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall")) p->fall = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.recover")) p->fall_recover = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.recovertime")) p->fall_recovertime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.damage")) p->fall_damage = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.xvelocity")) p->fall_vel_x = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.yvelocity")) p->fall_vel_y = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "priority")) p->priority = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "getpower"))
    {
        str8 rest;
        str8 hit = mcns_split_comma_first(val, &rest);
        p->getpower_hit = mugen_expr_parse(hit, alloc);
        if (rest.len > 0) p->getpower_guard = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "givepower"))
    {
        str8 rest;
        str8 hit = mcns_split_comma_first(val, &rest);
        p->givepower_hit = mugen_expr_parse(hit, alloc);
        if (rest.len > 0) p->givepower_guard = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "p1stateno")) p->p1stateno = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "p2stateno")) p->p2stateno = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "p1facing")) p->p1facing = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "p2facing")) p->p2facing = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "p2getp1state")) p->p2getp1state = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "numhits")) p->numhits = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "hitonce")) p->hitonce = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "forcestand")) p->forcestand = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "ground.cornerpush.veloff")) p->ground_cornerpush = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "air.cornerpush.veloff")) p->air_cornerpush = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "guard.cornerpush.veloff")) p->guard_cornerpush = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "yaccel")) p->yaccel = mugen_expr_parse(val, alloc);
}

static void hitvelset_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(HitVelSet_Params));
    HitVelSet_Params* p = sc->params;
    if (str8_ieq_cstr(key, "x")) p->x = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "y")) p->y = mugen_expr_parse(val, alloc);
}

static void hitfallset_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(HitFallSet_Params));
    HitFallSet_Params* p = sc->params;
    if (str8_ieq_cstr(key, "fall.xvel") || str8_ieq_cstr(key, "xvel")) { p->fall_xvel = mugen_expr_parse(val, alloc); p->xvel_set = 1; }
    else if (str8_ieq_cstr(key, "fall.yvel") || str8_ieq_cstr(key, "yvel")) { p->fall_yvel = mugen_expr_parse(val, alloc); p->yvel_set = 1; }
    else if (str8_ieq_cstr(key, "fall.recover")) p->fall_recover = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.recovertime")) p->fall_recovertime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.damage")) p->fall_damage = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.envshake.time")) p->fall_envshake_time = mugen_expr_parse(val, alloc);
}

static void hitdef_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    HitDef_Params* p = sc->params;
    if (!p) return;
    Mugen_HitDef_Result* r = &state->hitdef;
    *r = (Mugen_HitDef_Result){0};
    r->active = true;
    r->attr = p->attr;
    r->hitflag = p->hitflag ? p->hitflag : (MUGEN_HF_M | MUGEN_HF_A | MUGEN_HF_F);
    r->guardflag = p->guardflag;
    r->ground_type = p->ground_type;
    r->animtype = p->animtype;
    r->damage_hit = mcns_eval_or_default(p->damage_hit, state, 0);
    r->damage_guard = mcns_eval_or_default(p->damage_guard, state, 0);
    r->pausetime_p1 = (i32)mcns_eval_or_default(p->pausetime_p1, state, 0);
    r->pausetime_p2 = (i32)mcns_eval_or_default(p->pausetime_p2, state, 0);
    r->guard_pausetime_p1 = p->guard_pausetime_p1 ? (i32)mugen_expr_eval(p->guard_pausetime_p1, state) : r->pausetime_p1;
    r->guard_pausetime_p2 = p->guard_pausetime_p2 ? (i32)mugen_expr_eval(p->guard_pausetime_p2, state) : r->pausetime_p2;
    r->spark_x = mcns_eval_or_default(p->spark_x, state, 0);
    r->spark_y = mcns_eval_or_default(p->spark_y, state, 0);
    r->hitsound_group = (i32)mcns_eval_or_default(p->hitsound_group, state, 0);
    r->hitsound_index = (i32)mcns_eval_or_default(p->hitsound_index, state, 0);
    r->guardsound_group = (i32)mcns_eval_or_default(p->guardsound_group, state, 0);
    r->guardsound_index = (i32)mcns_eval_or_default(p->guardsound_index, state, 0);
    r->ground_slidetime = (i32)mcns_eval_or_default(p->ground_slidetime, state, 0);
    r->ground_hittime = (i32)mcns_eval_or_default(p->ground_hittime, state, 0);
    r->ground_vel_x = mcns_eval_or_default(p->ground_vel_x, state, 0);
    r->ground_vel_y = mcns_eval_or_default(p->ground_vel_y, state, 0);
    r->guard_velocity = mcns_eval_or_default(p->guard_velocity, state, r->ground_vel_x * -0.5f);
    r->guard_slidetime = (i32)mcns_eval_or_default(p->guard_slidetime, state, (f32)r->ground_slidetime);
    r->guard_hittime = (i32)mcns_eval_or_default(p->guard_hittime, state, (f32)r->guard_slidetime);
    r->guard_ctrltime = (i32)mcns_eval_or_default(p->guard_ctrltime, state, (f32)r->guard_slidetime);
    r->air_vel_x = mcns_eval_or_default(p->air_vel_x, state, 0);
    r->air_vel_y = mcns_eval_or_default(p->air_vel_y, state, 0);
    r->air_hittime = (i32)mcns_eval_or_default(p->air_hittime, state, 0);
    r->air_fall = mcns_eval_or_default(p->air_fall, state, 0) != 0.0f;
    r->fall = mcns_eval_or_default(p->fall, state, 0) != 0.0f;
    r->fall_recover = mcns_eval_or_default(p->fall_recover, state, 1) != 0.0f;
    r->fall_recovertime = (i32)mcns_eval_or_default(p->fall_recovertime, state, 4);
    r->fall_vel_x = mcns_eval_or_default(p->fall_vel_x, state, 0);
    r->fall_vel_y = mcns_eval_or_default(p->fall_vel_y, state, -4.5f);
    r->priority = (i32)mcns_eval_or_default(p->priority, state, 4);
    r->getpower_hit = (i32)mcns_eval_or_default(p->getpower_hit, state, 0);
    r->getpower_guard = (i32)mcns_eval_or_default(p->getpower_guard, state, 0);
    r->forcestand = mcns_eval_or_default(p->forcestand, state, 0) != 0.0f;
    r->hitonce = mcns_eval_or_default(p->hitonce, state, 0) != 0.0f;
    r->numhits = (i32)mcns_eval_or_default(p->numhits, state, 1);
    r->p1stateno = (i32)mcns_eval_or_default(p->p1stateno, state, -1);
    r->p2stateno = (i32)mcns_eval_or_default(p->p2stateno, state, -1);
    r->p2getp1state = mcns_eval_or_default(p->p2getp1state, state, 1) != 0.0f;
    r->p1facing = (i32)mcns_eval_or_default(p->p1facing, state, 0);
    r->p2facing = (i32)mcns_eval_or_default(p->p2facing, state, 0);
    r->juggle = state->current_juggle;
    r->ground_cornerpush_veloff = mcns_eval_or_default(p->ground_cornerpush, state, r->guard_velocity * -1.3f);
    r->air_cornerpush_veloff = mcns_eval_or_default(p->air_cornerpush, state, r->ground_cornerpush_veloff);
    r->guard_cornerpush_veloff = mcns_eval_or_default(p->guard_cornerpush, state, r->ground_cornerpush_veloff);
    if (p->yaccel)
    {
        r->yaccel = mugen_expr_eval(p->yaccel, state);
        r->has_yaccel = true;
    }
    state->hitdef_pending = true;
}

static void hitvelset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    HitVelSet_Params* p = sc->params;
    if (!p) return;
    if (p->x && mugen_expr_eval(p->x, state) != 0.0f)
        state->vel_x = state->ghv.xvel;
    if (p->y && mugen_expr_eval(p->y, state) != 0.0f)
        state->vel_y = state->ghv.yvel;
}

static void hitfallvel_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    (void)sc;
    state->vel_x = state->ghv.fall_xvel;
    state->vel_y = state->ghv.fall_yvel;
}

static void hitfalldamage_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    (void)sc;
    state->life -= (f32)state->ghv.fall_damage;
    if (state->life < 0) state->life = 0;
}

static void hitfallset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    HitFallSet_Params* p = sc->params;
    if (!p) return;
    if (p->xvel_set) state->ghv.fall_xvel = mugen_expr_eval(p->fall_xvel, state);
    if (p->yvel_set) state->ghv.fall_yvel = mugen_expr_eval(p->fall_yvel, state);
    if (p->fall_recover) state->ghv.fall_recover = mugen_expr_eval(p->fall_recover, state) != 0.0f;
    if (p->fall_recovertime) state->ghv.fall_recovertime = (i32)mugen_expr_eval(p->fall_recovertime, state);
    if (p->fall_damage) state->ghv.fall_damage = (i32)mugen_expr_eval(p->fall_damage, state);
}

__attribute__((constructor))
static void register_hit(void)
{
    mugen_sc_register(MUGEN_SC_HITDEF, "hitdef", hitdef_parse, hitdef_exec);
    mugen_sc_register(MUGEN_SC_HITVELSET, "hitvelset", hitvelset_parse, hitvelset_exec);
    mugen_sc_register(MUGEN_SC_HITFALLVEL, "hitfallvel", NULL, hitfallvel_exec);
    mugen_sc_register(MUGEN_SC_HITFALLDAMAGE, "hitfalldamage", NULL, hitfalldamage_exec);
    mugen_sc_register(MUGEN_SC_HITFALLSET, "hitfallset", hitfallset_parse, hitfallset_exec);
}
