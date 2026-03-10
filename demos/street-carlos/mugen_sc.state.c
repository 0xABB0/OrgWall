#include "mugen_cns_parse.h"

typedef struct {
    Mugen_Expr* value;
    Mugen_Expr* ctrl;
    Mugen_Expr* anim;
} ChangeState_Params;

typedef struct {
    Mugen_Expr* value;
} ChangeAnim_Params;

typedef struct {
    Mugen_Expr* value;
} Ctrl_Params;

typedef struct {
    i8 statetype;
    i8 movetype;
    i8 physics;
} StateTypeSet_Params;

typedef struct {
    Mugen_Expr* value;
    Mugen_Expr* ctrl;
    Mugen_Expr* anim;
} SelfState_Params;

typedef struct {
    Mugen_Expr* value;
} ChangeAnim2_Params;

static void changestate_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(ChangeState_Params));
    ChangeState_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "ctrl")) p->ctrl = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "anim")) p->anim = mugen_expr_parse(val, alloc);
}

static void changeanim_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(ChangeAnim_Params));
    ChangeAnim_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
}

static void ctrl_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(Ctrl_Params));
    Ctrl_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
}

static void statetypeset_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(StateTypeSet_Params));
    StateTypeSet_Params* p = sc->params;
    if (!p->statetype && !p->movetype && !p->physics)
    {
        p->statetype = -1;
        p->movetype = -1;
        p->physics = -1;
    }
    str8 v = mcns_trim(val);
    if (v.len == 0) return;
    u8 c = v.data[0];
    if (c >= 'a' && c <= 'z') c -= 32;
    if (str8_ieq_cstr(key, "statetype"))
    {
        if (c == 'S') p->statetype = MUGEN_PHYSICS_S;
        else if (c == 'C') p->statetype = MUGEN_PHYSICS_C;
        else if (c == 'A') p->statetype = MUGEN_PHYSICS_A;
    }
    else if (str8_ieq_cstr(key, "movetype"))
    {
        if (c == 'I') p->movetype = MUGEN_MOVETYPE_I;
        else if (c == 'A') p->movetype = MUGEN_MOVETYPE_A;
        else if (c == 'H') p->movetype = MUGEN_MOVETYPE_H;
    }
    else if (str8_ieq_cstr(key, "physics"))
    {
        if (c == 'S') p->physics = MUGEN_PHYSICS_S;
        else if (c == 'C') p->physics = MUGEN_PHYSICS_C;
        else if (c == 'A') p->physics = MUGEN_PHYSICS_A;
        else if (c == 'N') p->physics = MUGEN_PHYSICS_N;
    }
}

static void selfstate_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(SelfState_Params));
    SelfState_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "ctrl")) p->ctrl = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "anim")) p->anim = mugen_expr_parse(val, alloc);
}

static void changeanim2_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(ChangeAnim2_Params));
    ChangeAnim2_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
}

static void changestate_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    ChangeState_Params* p = sc->params;
    if (!p || !p->value) return;
    state->pending_state = (i32)mugen_expr_eval(p->value, state);
    if (p->ctrl) state->pending_ctrl = (i32)mugen_expr_eval(p->ctrl, state);
    if (p->anim) state->pending_anim = (i32)mugen_expr_eval(p->anim, state);
    state->state_changed = true;
}

static void changeanim_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    ChangeAnim_Params* p = sc->params;
    if (!p || !p->value) return;
    state->pending_anim = (i32)mugen_expr_eval(p->value, state);
    state->anim = (u32)state->pending_anim;
    state->animelem = 0;
    state->animtime = -999;
    state->animelemtime = 0;
}

static void ctrl_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Ctrl_Params* p = sc->params;
    if (!p || !p->value) return;
    state->ctrl = mugen_expr_eval(p->value, state) != 0.0f;
}

static void statetypeset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    StateTypeSet_Params* p = sc->params;
    if (!p) return;
    if (p->statetype >= 0) state->statetype = (u8)p->statetype;
    if (p->movetype >= 0) state->movetype = (u8)p->movetype;
    if (p->physics >= 0) state->physics = (u8)p->physics;
}

static void selfstate_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    SelfState_Params* p = sc->params;
    if (!p || !p->value) return;
    state->pending_state = (i32)mugen_expr_eval(p->value, state);
    if (p->ctrl) state->pending_ctrl = (i32)mugen_expr_eval(p->ctrl, state);
    if (p->anim) state->pending_anim = (i32)mugen_expr_eval(p->anim, state);
    state->state_changed = true;
    state->state_owner_cns = NULL;
    state->bound_to = NULL;
    state->use_owner_anim = false;
}

static void changeanim2_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    ChangeAnim2_Params* p = sc->params;
    if (!p || !p->value) return;
    i32 anim = (i32)mugen_expr_eval(p->value, state);
    state->anim = (u32)anim;
    state->pending_anim = anim;
    state->animelem = 0;
    state->animtime = -999;
    state->animelemtime = 0;
    state->use_owner_anim = true;
}

__attribute__((constructor))
static void register_state(void)
{
    mugen_sc_register(MUGEN_SC_CHANGESTATE, "changestate", changestate_parse, changestate_exec);
    mugen_sc_register(MUGEN_SC_CHANGEANIM, "changeanim", changeanim_parse, changeanim_exec);
    mugen_sc_register(MUGEN_SC_CTRL, "ctrlset", ctrl_parse, ctrl_exec);
    mugen_sc_register(MUGEN_SC_STATETYPESET, "statetypeset", statetypeset_parse, statetypeset_exec);
    mugen_sc_register(MUGEN_SC_SELFSTATE, "selfstate", selfstate_parse, selfstate_exec);
    mugen_sc_register(MUGEN_SC_CHANGEANIM2, "changeanim2", changeanim2_parse, changeanim2_exec);
}
