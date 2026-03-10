#include "mugen_cns_parse.h"

typedef struct {
    Mugen_Expr* x;
    Mugen_Expr* y;
} Vel_Params;

typedef struct {
    Mugen_Expr* x;
    Mugen_Expr* y;
} Pos_Params;

static void vel_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(Vel_Params));
    Vel_Params* p = sc->params;
    if (str8_ieq_cstr(key, "x")) p->x = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "y")) p->y = mugen_expr_parse(val, alloc);
}

static void pos_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(Pos_Params));
    Pos_Params* p = sc->params;
    if (str8_ieq_cstr(key, "x")) p->x = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "y")) p->y = mugen_expr_parse(val, alloc);
}

static void velset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Vel_Params* p = sc->params;
    if (!p) return;
    if (p->x) state->vel_x = mugen_expr_eval(p->x, state);
    if (p->y) state->vel_y = mugen_expr_eval(p->y, state);
}

static void veladd_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Vel_Params* p = sc->params;
    if (!p) return;
    if (p->x) state->vel_x += mugen_expr_eval(p->x, state);
    if (p->y) state->vel_y += mugen_expr_eval(p->y, state);
}

static void velmul_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Vel_Params* p = sc->params;
    if (!p) return;
    if (p->x) state->vel_x *= mugen_expr_eval(p->x, state);
    if (p->y) state->vel_y *= mugen_expr_eval(p->y, state);
}

static void posset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Pos_Params* p = sc->params;
    if (!p) return;
    if (p->x) state->pos_x = mugen_expr_eval(p->x, state);
    if (p->y) state->pos_y = mugen_expr_eval(p->y, state);
}

static void posadd_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Pos_Params* p = sc->params;
    if (!p) return;
    if (p->x) state->pos_x += mugen_expr_eval(p->x, state) * state->facing;
    if (p->y) state->pos_y += mugen_expr_eval(p->y, state);
}

static void posfreeze_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    (void)sc;
    state->pos_freeze = true;
}

static void gravity_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    (void)sc;
    state->vel_y += state->gravity;
}

__attribute__((constructor))
static void register_vel(void)
{
    mugen_sc_register(MUGEN_SC_VELSET, "velset", vel_parse, velset_exec);
    mugen_sc_register(MUGEN_SC_VELADD, "veladd", vel_parse, veladd_exec);
    mugen_sc_register(MUGEN_SC_VELMUL, "velmul", vel_parse, velmul_exec);
    mugen_sc_register(MUGEN_SC_POSSET, "posset", pos_parse, posset_exec);
    mugen_sc_register(MUGEN_SC_POSADD, "posadd", pos_parse, posadd_exec);
    mugen_sc_register(MUGEN_SC_POSFREEZE, "posfreeze", NULL, posfreeze_exec);
    mugen_sc_register(MUGEN_SC_GRAVITY, "gravity", NULL, gravity_exec);
}
