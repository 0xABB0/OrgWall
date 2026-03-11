#include "mugen.cns.parse.h"

typedef struct {
    Mugen_Expr* pos_x;
    Mugen_Expr* pos_y;
} TargetBind_Params;

typedef struct {
    Mugen_Expr* value;
} TargetState_Params;

typedef struct {
    Mugen_Expr* value;
    Mugen_Expr* kill;
    Mugen_Expr* absolute;
} TargetLifeAdd_Params;

typedef struct {
    Mugen_Expr* value;
} TargetFacing_Params;

typedef struct {
    Mugen_Expr* value;
} TargetPowerAdd_Params;

static void targetbind_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(TargetBind_Params));
    TargetBind_Params* p = sc->params;
    if (str8_ieq_cstr(key, "pos"))
    {
        str8 rest;
        str8 px = mcns_split_comma_first(val, &rest);
        p->pos_x = mugen_expr_parse(px, alloc);
        if (rest.len > 0) p->pos_y = mugen_expr_parse(rest, alloc);
    }
}

static void targetstate_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(TargetState_Params));
    TargetState_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
}

static void targetlifeadd_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(TargetLifeAdd_Params));
    TargetLifeAdd_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "kill")) p->kill = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "absolute")) p->absolute = mugen_expr_parse(val, alloc);
}

static void targetfacing_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(TargetFacing_Params));
    TargetFacing_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
}

static void targetpoweradd_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(TargetPowerAdd_Params));
    TargetPowerAdd_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
}

static void targetbind_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    TargetBind_Params* p = sc->params;
    if (state->target_count == 0) return;
    f32 ox = p && p->pos_x ? mugen_expr_eval(p->pos_x, state) : 0;
    f32 oy = p && p->pos_y ? mugen_expr_eval(p->pos_y, state) : 0;
    for (u32 ti = 0; ti < state->target_count; ti++)
    {
        Mugen_Char_State* t = state->targets[ti].state;
        t->pos_x = state->pos_x + ox * state->facing;
        t->pos_y = state->pos_y + oy;
        t->vel_x = 0;
        t->vel_y = 0;
        t->ghv.isbound = true;
    }
}

static void targetstate_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    TargetState_Params* p = sc->params;
    if (!p || !p->value || state->target_count == 0) return;
    i32 sno = (i32)mugen_expr_eval(p->value, state);
    for (u32 ti = 0; ti < state->target_count; ti++)
    {
        Mugen_Char_State* t = state->targets[ti].state;
        t->pending_state = sno;
        t->pending_ctrl = 0;
        t->state_changed = true;
        t->state_owner_cns = state->self_cns;
    }
}

static void targetlifeadd_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    TargetLifeAdd_Params* p = sc->params;
    if (!p || !p->value || state->target_count == 0) return;
    f32 dmg = mugen_expr_eval(p->value, state);
    bool absolute = p->absolute && mugen_expr_eval(p->absolute, state) != 0.0f;
    bool kill = p->kill ? mugen_expr_eval(p->kill, state) != 0.0f : true;
    for (u32 ti = 0; ti < state->target_count; ti++)
    {
        Mugen_Char_State* t = state->targets[ti].state;
        f32 d = absolute ? dmg : dmg * (t->defence_mul > 0 ? t->defence_mul : 1.0f);
        t->life += d;
        if (t->life > t->lifemax) t->life = t->lifemax;
        if (!kill && t->life < 1) t->life = 1;
        if (t->life < 0) t->life = 0;
    }
}

static void targetfacing_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    TargetFacing_Params* p = sc->params;
    if (!p || !p->value || state->target_count == 0) return;
    i32 val = (i32)mugen_expr_eval(p->value, state);
    for (u32 ti = 0; ti < state->target_count; ti++)
    {
        Mugen_Char_State* t = state->targets[ti].state;
        if (val > 0) t->facing = state->facing;
        else if (val < 0) t->facing = -state->facing;
    }
}

static void targetpoweradd_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    TargetPowerAdd_Params* p = sc->params;
    if (!p || !p->value || state->target_count == 0) return;
    for (u32 ti = 0; ti < state->target_count; ti++)
    {
        Mugen_Char_State* t = state->targets[ti].state;
        t->power += mugen_expr_eval(p->value, state);
        if (t->power > t->powermax) t->power = t->powermax;
        if (t->power < 0) t->power = 0;
    }
}

__attribute__((constructor))
static void register_target(void)
{
    mugen_sc_register(MUGEN_SC_TARGETBIND, "targetbind", targetbind_parse, targetbind_exec);
    mugen_sc_register(MUGEN_SC_TARGETSTATE, "targetstate", targetstate_parse, targetstate_exec);
    mugen_sc_register(MUGEN_SC_TARGETLIFEADD, "targetlifeadd", targetlifeadd_parse, targetlifeadd_exec);
    mugen_sc_register(MUGEN_SC_TARGETFACING, "targetfacing", targetfacing_parse, targetfacing_exec);
    mugen_sc_register(MUGEN_SC_TARGETPOWERADD, "targetpoweradd", targetpoweradd_parse, targetpoweradd_exec);
}
