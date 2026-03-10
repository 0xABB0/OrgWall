#include "mugen_cns_parse.h"

typedef struct {
    Mugen_Expr* id;
    Mugen_Expr* stateno;
    Mugen_Expr* pos_x;
    Mugen_Expr* pos_y;
    u8 postype;
    Mugen_Expr* facing;
    Mugen_Expr* ownpal;
} Helper_Params;

static void helper_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(Helper_Params));
    Helper_Params* p = sc->params;
    if (str8_ieq_cstr(key, "helperid") || str8_ieq_cstr(key, "id")) p->id = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "stateno")) p->stateno = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "pos"))
    {
        str8 rest;
        str8 px = mcns_split_comma_first(val, &rest);
        p->pos_x = mugen_expr_parse(px, alloc);
        if (rest.len > 0) p->pos_y = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "postype"))
    {
        str8 v = mcns_trim(val);
        if (str8_ieq_cstr(v, "p1"))         p->postype = MUGEN_POSTYPE_P1;
        else if (str8_ieq_cstr(v, "left"))   p->postype = MUGEN_POSTYPE_LEFT;
        else if (str8_ieq_cstr(v, "right"))  p->postype = MUGEN_POSTYPE_RIGHT;
        else if (str8_ieq_cstr(v, "back"))   p->postype = MUGEN_POSTYPE_BACK;
        else if (str8_ieq_cstr(v, "front"))  p->postype = MUGEN_POSTYPE_FRONT;
    }
    else if (str8_ieq_cstr(key, "facing")) p->facing = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "ownpal")) p->ownpal = mugen_expr_parse(val, alloc);
}

static void helper_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Helper_Params* p = sc->params;
    if (!p) return;
    state->helper_spawn_pending = true;
    state->helper_spawn_id = p->id ? (i32)mugen_expr_eval(p->id, state) : 0;
    state->helper_spawn_stateno = p->stateno ? (i32)mugen_expr_eval(p->stateno, state) : 0;
    state->helper_spawn_x = p->pos_x ? mugen_expr_eval(p->pos_x, state) : 0;
    state->helper_spawn_y = p->pos_y ? mugen_expr_eval(p->pos_y, state) : 0;
    state->helper_spawn_postype = p->postype;
    state->helper_spawn_facing = p->facing ? (i32)mugen_expr_eval(p->facing, state) : 1;
}

static void destroyself_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    (void)sc;
    state->destroy_self_pending = true;
}

__attribute__((constructor))
static void register_helper(void)
{
    mugen_sc_register(MUGEN_SC_HELPER, "helper", helper_parse, helper_exec);
    mugen_sc_register(MUGEN_SC_DESTROYSELF, "destroyself", NULL, destroyself_exec);
}
